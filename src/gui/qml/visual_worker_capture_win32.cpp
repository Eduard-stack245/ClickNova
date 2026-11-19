// SPDX-License-Identifier: MIT
#include "visual_worker_capture_win32.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifdef CLICKNOVA_VISUAL_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif
#ifdef CLICKNOVA_VISUAL_TESSERACT
#include <tesseract/baseapi.h>
#endif

namespace clicknova::gui::qml {
namespace vw = clicknova::visual::worker;
namespace {
#ifdef _WIN32
struct Pixels {int width{}; int height{}; std::vector<std::uint8_t> bgra;};
// Fail closed rather than capture a different app, a stale HWND or an overlay.
// GetDC(NULL) reads screen-visible pixels, not a hidden application window.
bool captureForeground(const vw::Request& req, Pixels& dst) {
    HWND window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(req.windowHandle));
    DWORD pid{};
    if (!IsWindow(window) || !GetWindowThreadProcessId(window, &pid) ||
        pid != req.processId || !IsWindowVisible(window) || IsIconic(window) ||
        GetForegroundWindow() != window || GetAncestor(window,GA_ROOT) != window)
        return false;
    RECT client{};
    POINT topLeft{0,0};
    if (!GetClientRect(window,&client) || !ClientToScreen(window,&topLeft))return false;
    const auto w=client.right-client.left,h=client.bottom-client.top;
    // Avoid huge screenshots, GDI allocations and overly expensive OCR.
    if(w<16||h<16||w>1920||h>1200||
       static_cast<std::int64_t>(w)*h>2'300'000) return false;
    // Reject partially covered windows. Child controls of the same top-level
    // window are fine, unrelated overlays are never captured into the preview.
    for(int yi=0;yi<3;++yi)for(int xi=0;xi<3;++xi){
        POINT p{topLeft.x+(w*(xi+1))/4,topLeft.y+(h*(yi+1))/4};
        HWND hit=WindowFromPoint(p);
        if(!hit||GetAncestor(hit,GA_ROOT)!=window)return false;
    }
    HDC screen=GetDC(nullptr);
    if(!screen)return false;
    HDC mem=CreateCompatibleDC(screen);
    if(!mem){ReleaseDC(nullptr,screen);return false;}
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth=w;
    bitmapInfo.bmiHeader.biHeight=-h; // top-down BGRA
    bitmapInfo.bmiHeader.biPlanes=1;
    bitmapInfo.bmiHeader.biBitCount=32;
    bitmapInfo.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    HBITMAP dib=CreateDIBSection(screen,&bitmapInfo,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dib){DeleteDC(mem);ReleaseDC(nullptr,screen);return false;}
    HGDIOBJ original=SelectObject(mem,dib);
    const BOOL copied=BitBlt(mem,0,0,w,h,screen,topLeft.x,topLeft.y,SRCCOPY);
    // Recheck after capture. A changed HWND/focus invalidates all pixels.
    DWORD afterPid{};
    const bool targetStillLive=IsWindow(window) &&
        GetWindowThreadProcessId(window,&afterPid) && afterPid==pid &&
        GetForegroundWindow()==window;
    if(copied && targetStillLive && bits){
        const auto byteCount=static_cast<std::size_t>(w)*static_cast<std::size_t>(h)*4;
        auto* raw=static_cast<const std::uint8_t*>(bits);
        dst.bgra.assign(raw,raw+byteCount);
        dst.width=w;dst.height=h;
    }
    SelectObject(mem,original);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr,screen);
    return copied && targetStillLive && !dst.bgra.empty();
}
std::filesystem::path executableDirectory() {
    std::array<wchar_t,32768> path{};
    const auto n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(n==0||n>=path.size())return {};
    return std::filesystem::path(path.data()).parent_path();
}
// Never accept an arbitrary path from QML. Only files installed by the user
// under the app's own data/templates directory can be read by this worker.
std::filesystem::path safeTemplate(const std::string& id) {
    if(!vw::safeTemplateId(id))return {};
    const auto base=executableDirectory();
    if(base.empty())return {};
    const auto dataDir=base/L"data";
    const auto dir=dataDir/L"templates";
    const auto file=dir/std::filesystem::path(id+".png");
    const auto dataAttr=GetFileAttributesW(dataDir.c_str());
    const auto dirAttr=GetFileAttributesW(dir.c_str());
    const auto fileAttr=GetFileAttributesW(file.c_str());
    if(dataAttr==INVALID_FILE_ATTRIBUTES||dirAttr==INVALID_FILE_ATTRIBUTES||
       fileAttr==INVALID_FILE_ATTRIBUTES||
       (dataAttr&FILE_ATTRIBUTE_REPARSE_POINT)||
       !(dataAttr&FILE_ATTRIBUTE_DIRECTORY)||
       (dirAttr&FILE_ATTRIBUTE_REPARSE_POINT)||
       (fileAttr&FILE_ATTRIBUTE_REPARSE_POINT)||
       !(dirAttr&FILE_ATTRIBUTE_DIRECTORY)||(fileAttr&FILE_ATTRIBUTE_DIRECTORY))return {};
    std::error_code ec;
    const auto bytes=std::filesystem::file_size(file,ec);
    if(ec||bytes<24||bytes>4*1024*1024)return {};
    return file;
}
#ifdef CLICKNOVA_VISUAL_OPENCV
vw::Status inspectImage(const vw::Request& req,const Pixels& frame) {
    const auto file=safeTemplate(req.query);
    if(file.empty())return vw::Status::Unavailable;
    std::ifstream input(file,std::ios::binary);
    if(!input)return vw::Status::Unavailable;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                     std::istreambuf_iterator<char>{});
    // Header-size check BEFORE image decompression avoids tiny PNG bombs.
    static constexpr std::array<std::uint8_t,8> png{137,80,78,71,13,10,26,10};
    if(bytes.size()<24||!std::equal(png.begin(),png.end(),bytes.begin()))
        return vw::Status::Unavailable;
    auto pngU32=[&](std::size_t i){return (std::uint32_t(bytes[i])<<24)|
        (std::uint32_t(bytes[i+1])<<16)|(std::uint32_t(bytes[i+2])<<8)|bytes[i+3];};
    const auto w=pngU32(16),h=pngU32(20);
    if(w<4||h<4||w>1024||h>1024||static_cast<std::uint64_t>(w)*h>1'000'000)
        return vw::Status::Unavailable;
    cv::Mat templ=cv::imdecode(bytes,cv::IMREAD_GRAYSCALE);
    if(templ.empty()||templ.cols!=static_cast<int>(w)||templ.rows!=static_cast<int>(h)||
       templ.cols>frame.width||templ.rows>frame.height)return vw::Status::Unavailable;
    cv::Scalar mean{},sd{};cv::meanStdDev(templ,mean,sd);
    if(sd[0]<2.0)return vw::Status::Unavailable; // refuse uniform templates
    cv::Mat bgra(frame.height,frame.width,CV_8UC4,
                 const_cast<std::uint8_t*>(frame.bgra.data()));
    cv::Mat gray;cv::cvtColor(bgra,gray,cv::COLOR_BGRA2GRAY);
    cv::Mat scores;cv::matchTemplate(gray,templ,scores,cv::TM_CCOEFF_NORMED);
    double best{};cv::minMaxLoc(scores,nullptr,&best,nullptr,nullptr);
    return best>=0.94 ? vw::Status::Found : vw::Status::NotFound;
}
#endif
#ifdef CLICKNOVA_VISUAL_TESSERACT
vw::Status inspectOcr(const vw::Request& req,const Pixels& frame) {
    const auto base=executableDirectory();
    const auto dataDir=base/L"data";
    const auto tessdata=dataDir/L"tessdata";
    const auto dataAttr=GetFileAttributesW(dataDir.c_str());
    const auto tessAttr=GetFileAttributesW(tessdata.c_str());
    if(dataAttr==INVALID_FILE_ATTRIBUTES||tessAttr==INVALID_FILE_ATTRIBUTES||
       (dataAttr&FILE_ATTRIBUTE_REPARSE_POINT)||
       (tessAttr&FILE_ATTRIBUTE_REPARSE_POINT))return vw::Status::Unavailable;
    std::string root;
    std::string languages="eng";
    std::error_code ec;
    if(std::filesystem::exists(tessdata/L"eng.traineddata",ec)) {
        const auto utf8=tessdata.u8string();
        root.assign(utf8.begin(),utf8.end());
        if(std::filesystem::exists(tessdata/L"ukr.traineddata",ec))languages+="+ukr";
        if(std::filesystem::exists(tessdata/L"rus.traineddata",ec))languages+="+rus";
    }
    if(root.empty())return vw::Status::Unavailable;
    tesseract::TessBaseAPI ocr;
    if(ocr.Init(root.c_str(),languages.c_str())!=0)
        return vw::Status::Unavailable;
    // OCR receives pixels directly; no private screenshot or OCR text on disk.
    ocr.SetImage(frame.bgra.data(),frame.width,frame.height,4,frame.width*4);
    char* recognized=ocr.GetUTF8Text();
    if(!recognized)return vw::Status::Failed;
    constexpr std::size_t maxText=256*1024;
    std::size_t length=0;
    while(length<=maxText && recognized[length]!=0)++length;
    bool found=false;
    if(length<=maxText) {
        const std::string_view page(recognized,length);
        found=page.find(req.query)!=std::string_view::npos;
    }
    delete[] recognized;
    return length>maxText?vw::Status::Failed:
        (found?vw::Status::Found:vw::Status::NotFound);
}
#endif
#endif // _WIN32
} // namespace

vw::Reply inspectVisualTarget(const vw::Request& request) noexcept {
    vw::Reply reply{request.provider,request.nonce,vw::Status::InvalidRequest,0};
    if(!vw::valid(request))return reply;
#ifndef _WIN32
    reply.status=vw::Status::Unavailable;
#else
    try {
        // Optional libraries are never replaced with an unrelated provider.
        if(request.provider==vw::Provider::Image){
#ifndef CLICKNOVA_VISUAL_OPENCV
            reply.status=vw::Status::Unavailable;return reply;
#endif
        } else {
#ifndef CLICKNOVA_VISUAL_TESSERACT
            reply.status=vw::Status::Unavailable;return reply;
#endif
        }
        Pixels screenshot;
        if(!captureForeground(request,screenshot)){
            reply.status=vw::Status::UnsafeTarget;return reply;
        }
#ifdef CLICKNOVA_VISUAL_OPENCV
        if(request.provider==vw::Provider::Image)
            reply.status=inspectImage(request,screenshot);
#endif
#ifdef CLICKNOVA_VISUAL_TESSERACT
        if(request.provider==vw::Provider::Ocr)
            reply.status=inspectOcr(request,screenshot);
#endif
    } catch(...) {reply.status=vw::Status::Failed;}
#endif
    reply.matches=reply.status==vw::Status::Found?1:0;
    return reply;
}
} // namespace clicknova::gui::qml
