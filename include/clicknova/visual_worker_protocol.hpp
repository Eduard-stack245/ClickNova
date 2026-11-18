// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>
#include <utility>

// Single-use, read-only Windows visual preview IPC. No images, text recognized
// by OCR, selector payloads, file paths, input commands or cursor coordinates
// can appear in a reply. The user's query travels only to a child via stdin.
namespace clicknova::visual::worker {
constexpr std::uint32_t version = 1;
constexpr std::uint32_t requestMagic = 0x51565743;  // CWVQ
constexpr std::uint32_t responseMagic = 0x52565743; // CWVR
constexpr std::size_t maxFrame = 4096;
enum class Provider : std::uint32_t { Image = 3, Ocr = 4 };
enum class Status : std::uint32_t {
    Found = 1, NotFound = 2, Unavailable = 3, UnsafeTarget = 4,
    InvalidRequest = 5, Failed = 6
};
struct Request {
    Provider provider{Provider::Ocr};
    std::uint64_t nonce{};
    std::uint64_t windowHandle{};
    std::uint32_t processId{};
    std::string query; // OCR UTF-8 exact substring OR safe template filename stem
};
struct Reply {
    Provider provider{Provider::Ocr};
    std::uint64_t nonce{};
    Status status{Status::Failed};
    std::uint32_t matches{}; // 0 or 1; never text, geometry or pixel contents
};
inline bool validProvider(Provider p) noexcept { return p == Provider::Image || p == Provider::Ocr; }
inline bool validStatus(Status s) noexcept {
    return s >= Status::Found && s <= Status::Failed;
}
inline bool safeTemplateId(const std::string& s) noexcept {
    if(s.empty() || s.size()>48) return false;
    for(unsigned char c:s) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
        (c>='0'&&c<='9')||c=='-'||c=='_')) return false;
    return true;
}
// Strict Unicode validation: refuse invalid UTF-8, control characters and
// bidi override controls in user-supplied OCR queries.
inline bool safeOcrQuery(const std::string& s) noexcept {
    if(s.empty()||s.size()>160)return false;
    std::size_t i=0;
    while(i<s.size()) {
        const auto a=static_cast<unsigned char>(s[i++]);
        std::uint32_t cp{}; unsigned need{};
        if(a<0x80){cp=a;}
        else if(a>=0xC2&&a<=0xDF){cp=a&0x1F;need=1;}
        else if(a>=0xE0&&a<=0xEF){cp=a&0x0F;need=2;}
        else if(a>=0xF0&&a<=0xF4){cp=a&7;need=3;}
        else return false;
        if(i+need>s.size())return false;
        for(unsigned j=0;j<need;++j){
            auto b=static_cast<unsigned char>(s[i++]);
            if((b&0xC0)!=0x80)return false;
            cp=(cp<<6)|(b&0x3F);
        }
        if((need==1&&cp<0x80)||(need==2&&cp<0x800)||
           (need==3&&cp<0x10000)||cp>0x10FFFF||
           (cp>=0xD800&&cp<=0xDFFF)||cp<0x20||
           (cp>=0x7F&&cp<=0x9F)||
           (cp>=0x202A&&cp<=0x202E)||
           (cp>=0x2066&&cp<=0x2069))return false;
    }
    return true;
}
inline bool valid(const Request& r) noexcept {
    return validProvider(r.provider) && r.nonce != 0 && r.windowHandle != 0 &&
       r.windowHandle <= static_cast<std::uint64_t>(std::numeric_limits<std::uintptr_t>::max()) &&
       r.processId != 0 && (r.provider==Provider::Image ?
       safeTemplateId(r.query) : safeOcrQuery(r.query));
}
inline bool valid(const Reply& r) noexcept {
    return validProvider(r.provider)&&r.nonce!=0&&validStatus(r.status)&&
           (r.status==Status::Found?r.matches==1:r.matches==0);
}
namespace detail {
struct Writer {
    std::vector<std::uint8_t> b;
    void u32(std::uint32_t v){for(unsigned i=0;i<4;++i)b.push_back(static_cast<std::uint8_t>(v>>(8*i)));}
    void u64(std::uint64_t v){for(unsigned i=0;i<8;++i)b.push_back(static_cast<std::uint8_t>(v>>(8*i)));}
    void str(const std::string& s){u32(static_cast<std::uint32_t>(s.size()));b.insert(b.end(),s.begin(),s.end());}
};
struct Reader {
    std::span<const std::uint8_t> b;std::size_t at{};
    bool u32(std::uint32_t& v){
        if(at>b.size()||b.size()-at<4)return false;
        v=0;for(unsigned i=0;i<4;++i)v|=static_cast<std::uint32_t>(b[at++])<<(8*i);return true;
    }
    bool u64(std::uint64_t& v){
        if(at>b.size()||b.size()-at<8)return false;
        v=0;for(unsigned i=0;i<8;++i)v|=static_cast<std::uint64_t>(b[at++])<<(8*i);return true;
    }
    bool str(std::string& s){
        std::uint32_t n{};if(!u32(n)||n>160||at>b.size()||b.size()-at<n)return false;
        s.assign(reinterpret_cast<const char*>(b.data()+at),n);at+=n;return true;
    }
    bool done()const{return at==b.size();}
};
}
inline bool encode(const Request& r,std::vector<std::uint8_t>& out){
    if(!valid(r))return false;
    detail::Writer w;w.u32(requestMagic);w.u32(version);w.u32(static_cast<std::uint32_t>(r.provider));
    w.u64(r.nonce);w.u64(r.windowHandle);w.u32(r.processId);w.str(r.query);
    if(w.b.size()>maxFrame)return false;out=std::move(w.b);return true;
}
inline bool decode(std::span<const std::uint8_t> b,Request& out){
    if(b.empty()||b.size()>maxFrame)return false;
    detail::Reader r{b};std::uint32_t magic{},v{},p{},pid{};Request candidate;
    if(!r.u32(magic)||magic!=requestMagic||!r.u32(v)||v!=version||
       !r.u32(p)||!r.u64(candidate.nonce)||!r.u64(candidate.windowHandle)||
       !r.u32(pid)||!r.str(candidate.query)||!r.done())return false;
    candidate.provider=static_cast<Provider>(p);candidate.processId=pid;
    if(!valid(candidate))return false;out=std::move(candidate);return true;
}
inline bool encode(const Reply& r,std::vector<std::uint8_t>& out){
    if(!valid(r))return false;
    detail::Writer w;w.u32(responseMagic);w.u32(version);w.u32(static_cast<std::uint32_t>(r.provider));
    w.u64(r.nonce);w.u32(static_cast<std::uint32_t>(r.status));w.u32(r.matches);
    out=std::move(w.b);return true;
}
inline bool decode(std::span<const std::uint8_t> b,Reply& out){
    if(b.empty()||b.size()>maxFrame)return false;
    detail::Reader r{b};std::uint32_t magic{},v{},p{},st{};Reply candidate;
    if(!r.u32(magic)||magic!=responseMagic||!r.u32(v)||v!=version||
       !r.u32(p)||!r.u64(candidate.nonce)||!r.u32(st)||!r.u32(candidate.matches)||!r.done())return false;
    candidate.provider=static_cast<Provider>(p);candidate.status=static_cast<Status>(st);
    if(!valid(candidate))return false;out=candidate;return true;
}
inline bool matches(const Request& request,const Reply& reply) noexcept {
    return valid(request)&&valid(reply)&&request.nonce==reply.nonce&&
        request.provider==reply.provider;
}
inline bool frame(std::span<const std::uint8_t> payload,std::vector<std::uint8_t>& out){
    if(payload.empty()||payload.size()>maxFrame)return false;
    detail::Writer w;w.u32(static_cast<std::uint32_t>(payload.size()));
    w.b.insert(w.b.end(),payload.begin(),payload.end());out=std::move(w.b);return true;
}
inline bool unframe(std::span<const std::uint8_t> bytes,std::vector<std::uint8_t>& out){
    if(bytes.size()<4||bytes.size()>maxFrame+4)return false;
    std::uint32_t n=static_cast<std::uint32_t>(bytes[0])|
       (static_cast<std::uint32_t>(bytes[1])<<8)|
       (static_cast<std::uint32_t>(bytes[2])<<16)|
       (static_cast<std::uint32_t>(bytes[3])<<24);
    if(n==0||n>maxFrame||bytes.size()!=static_cast<std::size_t>(n)+4)return false;
    out.assign(bytes.begin()+4,bytes.end());return true;
}
} // namespace clicknova::visual::worker
