// SPDX-License-Identifier: Apache-2.0
#include "clicknova/visual_recognition.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <new>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <optional>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cwctype>

#if defined(_WIN32) && defined(CLICKNOVA_WITH_OPENCV)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#ifdef CLICKNOVA_WITH_TESSERACT
#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>
#endif
#endif

namespace clicknova::accessibility::visual {
namespace {
bool digitParse(const std::wstring& s, int& value) {
    if (s.empty() || s.size() > 5) return false;
    int v = 0;
    for (wchar_t c : s) {
        if (c < L'0' || c > L'9') return false;
        v = v * 10 + (c - L'0');
    }
    if (v > 10000) return false;
    value = v;
    return true;
}
// Name-only defaults are used exclusively for scenarios created by older builds.
constexpr int kLegacyImageConfidence = 90;
constexpr int kLegacyOcrConfidence = 60;
constexpr int kLegacyRadiusPercent = 8;

// Portable image templates live under data/templates and are referenced by a
// content-addressed filename. A fixed-size token cannot escape that folder.
// Legacy absolute paths remain readable for older saved scenarios.
bool portableTemplateToken(const std::wstring& path) {
    constexpr std::wstring_view prefix = L"asset:";
    if (!path.starts_with(prefix)) return false;
    const auto filename = std::wstring_view(path).substr(prefix.size());
    const auto dot = filename.find(L'.');
    if (dot != 64 || filename.find(L'.', dot + 1) != std::wstring_view::npos)
        return false;
    for (wchar_t ch : filename.substr(0, dot)) {
        if (!((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f')))
            return false;
    }
    const auto ext = filename.substr(dot);
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp";
}

std::filesystem::path resolveTemplateFile(const std::wstring& path) {
    if (!portableTemplateToken(path)) return std::filesystem::path(path);
#if defined(_WIN32) && defined(CLICKNOVA_WITH_OPENCV)
    const DWORD required = GetEnvironmentVariableW(L"CLICKNOVA_ASSET_ROOT", nullptr, 0);
    if (required < 2 || required > 32768) return {};
    std::wstring root(static_cast<std::size_t>(required), L'\0');
    const DWORD size = GetEnvironmentVariableW(L"CLICKNOVA_ASSET_ROOT", root.data(), required);
    if (size == 0 || size >= required) return {};
    root.resize(size);
    const std::filesystem::path directory(root);
    if (!directory.is_absolute()) return {};
    return directory / path.substr(6);
#else
    return {}; // Windows-only visual engine; Linux backend is not implemented.
#endif
}

std::wstring folded(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t c) {
                       if (c >= L'А' && c <= L'Я') return static_cast<wchar_t>(c + 32);
                       if (c == L'Ё') return L'ё';
                       return static_cast<wchar_t>(std::towlower(c));
                   });
    return text;
}
#if defined(_WIN32) && defined(CLICKNOVA_WITH_OPENCV)
std::wstring utf8Wide(const char* text) {
    if (!text) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (n <= 1 || n > 4096) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out.data(), n)) return {};
    out.pop_back();
    return out;
}
struct TargetFrame {
    cv::Mat bgr;
    POINT origin{};
};
// A chosen image must still exist when the user searches or plays a scenario.
// OpenCV's empty-image return is not an actionable error message.
std::string templateError(const std::wstring& path) {
    if (!validTemplatePath(path)) return "Select an absolute PNG/JPEG/BMP template file.";
    std::error_code ec;
    const std::filesystem::path file = resolveTemplateFile(path);
    if (file.empty() || !std::filesystem::is_regular_file(file, ec) || ec)
        return "Image template file not found or not readable.";
    const auto bytes = std::filesystem::file_size(file, ec);
    if (ec || bytes < 64 || bytes > 8 * 1024 * 1024)
        return "Image template must be between 64 bytes and 8 MB.";
    return {};
}
// Visible on-screen pixels are captured. Some exclusive-fullscreen and
// protected graphics surfaces cannot be read this way; we fail rather than
// reading other windows or injecting unverified input.
bool capture(const clicknova::platform::WindowTarget& target, TargetFrame& frame) {
    const HWND hwnd = reinterpret_cast<HWND>(target.handle);
    DWORD pid = 0;
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd) ||
        !GetWindowThreadProcessId(hwnd, &pid) || !pid || pid != target.processId ||
        pid == GetCurrentProcessId() ||
        GetAncestor(GetForegroundWindow(), GA_ROOT) != GetAncestor(hwnd, GA_ROOT)) return false;
    RECT rect{};
    if (!GetClientRect(hwnd, &rect)) return false;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    // Bound memory by total screenshot pixels, without assuming a particular
    // game's resolution or a developer's display size.
    constexpr std::int64_t kMaxCapturePixels = 24LL * 1024 * 1024;
    if (width < 16 || height < 16 ||
        static_cast<std::int64_t>(width) * height > kMaxCapturePixels) return false;
    POINT start{};
    if (!ClientToScreen(hwnd, &start)) return false;
    HDC desktop = GetDC(nullptr);
    if (!desktop) return false;
    HDC memory = CreateCompatibleDC(desktop);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = memory ? CreateDIBSection(desktop, &info, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
    bool success = false;
    if (bitmap && pixels) {
        HGDIOBJ old = SelectObject(memory, bitmap);
        if (old && old != HGDI_ERROR &&
            BitBlt(memory, 0, 0, width, height, desktop, start.x, start.y, SRCCOPY | CAPTUREBLT)) {
            cv::Mat bgra(height, width, CV_8UC4, pixels);
            try {
                cv::cvtColor(bgra, frame.bgr, cv::COLOR_BGRA2BGR);
                frame.origin = start;
                success = !frame.bgr.empty();
            } catch (const cv::Exception&) {
                success = false;
            } catch (const std::bad_alloc&) {
                success = false;
            }
        }
        if (old && old != HGDI_ERROR) SelectObject(memory, old);
    }
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    ReleaseDC(nullptr, desktop);
    return success;
}
struct Match { cv::Rect rect; float confidence{0}; };
std::vector<Match> templates(const cv::Mat& picture, const std::wstring& path, int maxResults,
                             int scorePercent, std::string* error = nullptr) {
    std::vector<Match> found;
    if (const auto problem = templateError(path); !problem.empty()) {
        if (error) *error = problem;
        return found;
    }
    // imdecode handles non-ASCII Windows paths, unlike imread with narrow path.
    const auto resolvedPath = resolveTemplateFile(path);
    std::unique_ptr<FILE, decltype(&fclose)> file(_wfopen(resolvedPath.c_str(), L"rb"), &fclose);
    if (!file) {
        if (error) *error = "Image template could not be opened.";
        return found;
    }
    std::vector<unsigned char> buffer;
    try {
        if (_fseeki64(file.get(), 0, SEEK_END) != 0) {
            if (error) *error = "Image template could not be read.";
            return found;
        }
        const auto length = _ftelli64(file.get());
        if (length < 64 || length > 8 * 1024 * 1024 ||
            _fseeki64(file.get(), 0, SEEK_SET) != 0) {
            if (error) *error = "Image template has an invalid size.";
            return found;
        }
        buffer.resize(static_cast<std::size_t>(length));
        if (fread(buffer.data(), 1, buffer.size(), file.get()) != buffer.size()) {
            if (error) *error = "Image template could not be read completely.";
            return found;
        }
        file.reset();
        const auto pattern = cv::imdecode(buffer, cv::IMREAD_COLOR);
        if (pattern.empty()) {
            if (error) *error = "Image template format is invalid or corrupted.";
            return found;
        }
        if (pattern.cols < 12 || pattern.rows < 12 ||
            pattern.cols > picture.cols || pattern.rows > picture.rows ||
            pattern.cols > 1024 || pattern.rows > 1024) {
            if (error) *error = "Image template dimensions must be 12..1024 pixels and fit within the target window.";
            return found;
        }
        cv::Scalar mean, variance;
        cv::meanStdDev(pattern, mean, variance);
        if (variance[0] + variance[1] + variance[2] < 30) {
            if (error) *error = "Image template is almost uniform; select a crop with distinctive details.";
            return found;
        }
        cv::Mat scores;
        cv::matchTemplate(picture, pattern, scores, cv::TM_CCOEFF_NORMED);
        for (int i = 0; i < maxResults; ++i) {
            double confidence = 0;
            cv::Point position;
            cv::minMaxLoc(scores, nullptr, &confidence, nullptr, &position);
            if (!std::isfinite(confidence) || confidence < scorePercent / 100.0) break;
            found.push_back({cv::Rect(position, pattern.size()), static_cast<float>(confidence)});
            // Non-maximum suppression: duplicates of the same visual area are not separate buttons.
            const cv::Rect excluded(position.x - pattern.cols / 2, position.y - pattern.rows / 2,
                                    pattern.cols * 2, pattern.rows * 2);
            const cv::Rect valid = excluded & cv::Rect(0, 0, scores.cols, scores.rows);
            if (!valid.empty()) scores(valid).setTo(-1.0f);
        }
    } catch (const cv::Exception&) {
        if (error) *error = "OpenCV could not decode or match the image template.";
        return {};
    } catch (const std::bad_alloc&) {
        if (error) *error = "Not enough memory for image matching.";
        return {};
    }
    return found;
}
#ifdef CLICKNOVA_WITH_TESSERACT
// Game HUDs often consist of a few short lines separated by huge areas of
// background graphics. Full-frame PSM_AUTO misses them. Propose compact,
// bounded text regions from edges and recognize each as a single line.
std::vector<cv::Rect> proposeTextRegions(const cv::Mat& gray) {
    cv::Mat edges, connected;
    cv::Canny(gray, edges, 50, 140);
    const int kernelWidth = std::clamp(gray.cols / 90, 9, 31);
    const int kernelHeight = std::clamp(gray.rows / 140, 3, 9);
    cv::dilate(edges, connected, cv::getStructuringElement(
        cv::MORPH_RECT, cv::Size(kernelWidth, kernelHeight)));
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(connected, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    std::vector<cv::Rect> regions;
    regions.reserve(std::min<std::size_t>(contours.size(), 100));
    const cv::Rect screen(0, 0, gray.cols, gray.rows);
    for (const auto& contour : contours) {
        const cv::Rect r = cv::boundingRect(contour);
        if (r.width < 35 || r.height < 12 ||
            r.width > gray.cols * 9 / 10 || r.height > gray.rows / 3 ||
            r.height > 240) continue;
        const int pad = std::clamp(r.height / 8, 4, 12);
        const cv::Rect padded(r.x - pad, r.y - pad,
                              r.width + 2 * pad, r.height + 2 * pad);
        regions.push_back(padded & screen);
    }
    // Order by screen position, not contrast/size, and bound work so an
    // animated full-screen texture cannot monopolize the OCR worker.
    std::sort(regions.begin(), regions.end(), [](const cv::Rect& l, const cv::Rect& r) {
        return l.y != r.y ? l.y < r.y : l.x < r.x;
    });
    constexpr std::size_t kMaxRegions = 64;
    if (regions.size() > kMaxRegions) regions.resize(kMaxRegions);
    return regions;
}

std::vector<Match> ocr(const cv::Mat& picture, const std::wstring& requested,
                       int limit, int scorePercent, std::vector<std::wstring>& labels,
                       std::string* error) {
    std::vector<Match> found;
    if (requested.empty() || requested.size() > 500) return found;
    try {
    tesseract::TessBaseAPI api;
    const bool needsRussian = std::any_of(requested.begin(), requested.end(), [](wchar_t ch) {
        return (ch >= L'А' && ch <= L'я') || ch == L'Ё' || ch == L'ё';
    });
    // Match the query's script rather than having English OCR transliterate
    // Cyrillic letters. Override with CLICKNOVA_OCR_LANGUAGES for other languages.
    const char* configured = std::getenv("CLICKNOVA_OCR_LANGUAGES");
    const bool customLanguages = configured && *configured;
    std::string languages = customLanguages ? configured : (needsRussian ? "rus" : "eng");
    if (languages.empty() || languages.size() > 80 ||
        !std::all_of(languages.begin(), languages.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_' || c == '+';
        })) {
        if (error) *error = "Invalid CLICKNOVA_OCR_LANGUAGES: use Tesseract language codes separated by +.";
        return found;
    }
    if (api.Init(nullptr, languages.c_str()) != 0) {
        if (error) *error = "Tesseract language data unavailable: check TESSDATA_PREFIX and install the requested .traineddata.";
        return found;
    }
    cv::Mat gray;
    cv::cvtColor(picture, gray, cv::COLOR_BGR2GRAY);
    const auto regions = proposeTextRegions(gray);
    auto appendRegion = [&](const cv::Rect& roi, tesseract::PageSegMode segmentation) {
        const cv::Mat crop = gray(roi);
        api.Clear();
        api.SetPageSegMode(segmentation);
        api.SetImage(crop.data, crop.cols, crop.rows, 1, static_cast<int>(crop.step));
        if (api.Recognize(nullptr) != 0) return;
        auto* line = api.GetIterator();
        if (!line) return;
        do {
            const char* raw = line->GetUTF8Text(tesseract::RIL_TEXTLINE);
            const auto text = utf8Wide(raw);
            delete[] raw;
            if (text.empty() || folded(text).find(folded(requested)) == std::wstring::npos) continue;
            int l = 0, t = 0, r = 0, b = 0;
            if (!line->BoundingBox(tesseract::RIL_TEXTLINE, &l, &t, &r, &b) ||
                r <= l || b <= t) continue;
            const float score = line->Confidence(tesseract::RIL_TEXTLINE) / 100.0f;
            if (score < scorePercent / 100.0f) continue;
            const cv::Rect textBox(l + roi.x, t + roi.y, r - l, b - t);
            if ((textBox & cv::Rect(0, 0, gray.cols, gray.rows)).empty()) continue;
            // Avoid returning an identical line if multiple region proposals overlap.
            bool duplicate = false;
            for (const auto& prior : found) {
                const auto overlap = prior.rect & textBox;
                if (overlap.area() * 2 >= std::min(prior.rect.area(), textBox.area())) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
            found.push_back({textBox, score});
            labels.push_back(text.substr(0, 512));
        } while (static_cast<int>(found.size()) < limit && line->Next(tesseract::RIL_TEXTLINE));
    };
    for (const auto& roi : regions) {
        if (static_cast<int>(found.size()) >= limit) break;
        appendRegion(roi, tesseract::PSM_SINGLE_LINE);
    }
    // For applications with conventional text pages and no separated regions,
    // preserve a standard full-image OCR fallback.
    if (found.empty()) appendRegion(cv::Rect(0, 0, gray.cols, gray.rows),
                                    tesseract::PSM_SPARSE_TEXT);
    return found;
    } catch (const cv::Exception&) {
        if (error) *error = "OpenCV failed while preparing OCR regions.";
    } catch (const std::bad_alloc&) {
        if (error) *error = "Not enough memory for OCR recognition.";
    }
    return {};
}
#endif
std::vector<Match> recognize(const cv::Mat& img, Provider provider,
                             const std::wstring& selector, int limit,
                             std::vector<std::wstring>& labels, std::string* error = nullptr) {
    std::wstring payload;
    int confidence = 0, radius = 0;
    if (!parseSelector(provider, selector, payload, confidence, radius)) return {};
    if (provider == Provider::Visual) {
        auto found = templates(img, payload, limit, confidence, error);
        labels.assign(found.size(), std::filesystem::path(payload).filename().wstring());
        return found;
    }
#ifdef CLICKNOVA_WITH_TESSERACT
    if (provider == Provider::Ocr)
        return ocr(img, payload, limit, confidence, labels, error);
#else
    (void)img; (void)limit; (void)labels;
#endif
    return {};
}
#endif
} // namespace

std::wstring makeSelector(Provider provider, const std::wstring& payload,
                          int confidence, int radiusPercent) {
    const bool image = provider == Provider::Visual;
    if ((!image && provider != Provider::Ocr) || payload.empty() ||
        payload.size() > 500 ||
        confidence < (image ? 50 : 30) || confidence > 99 ||
        radiusPercent < 1 || radiusPercent > 30 ||
        (image && !validTemplatePath(payload)) ||
        payload.find(L'\0') != std::wstring::npos) return {};
    return (image ? L"cv:@" : L"ocr:@") + std::to_wstring(confidence) + L"," +
           std::to_wstring(radiusPercent) + L"|" + payload;
}

bool parseSelector(Provider provider, const std::wstring& selector,
                   std::wstring& payload, int& confidence, int& radiusPercent) {
    if (provider != Provider::Visual && provider != Provider::Ocr) return false;
    const std::wstring prefix = provider == Provider::Visual ? L"cv:" : L"ocr:";
    if (!selector.starts_with(prefix) || selector.size() > 512) return false;
    std::wstring body = selector.substr(prefix.size());
    int score = provider == Provider::Visual ? kLegacyImageConfidence : kLegacyOcrConfidence;
    int radius = kLegacyRadiusPercent;
    if (body.starts_with(L"@")) {
        const auto comma = body.find(L',', 1);
        const auto separator = body.find(L'|', comma == std::wstring::npos ? 0 : comma + 1);
        if (comma == std::wstring::npos || separator == std::wstring::npos ||
            !digitParse(body.substr(1, comma - 1), score) ||
            !digitParse(body.substr(comma + 1, separator - comma - 1), radius)) return false;
        body = body.substr(separator + 1);
    }
    if (body.empty() || body.size() > 500 ||
        score < (provider == Provider::Visual ? 50 : 30) || score > 99 ||
        radius < 1 || radius > 30 ||
        body.find(L'\0') != std::wstring::npos ||
        (provider == Provider::Visual && !validTemplatePath(body))) return false;
    payload = std::move(body);
    confidence = score;
    radiusPercent = radius;
    return true;
}

bool imageAvailable() {
#if defined(_WIN32) && defined(CLICKNOVA_WITH_OPENCV)
    return true;
#else
    return false;
#endif
}
bool ocrAvailable() {
#if defined(_WIN32) && defined(CLICKNOVA_WITH_OPENCV) && defined(CLICKNOVA_WITH_TESSERACT)
    return true;
#else
    return false;
#endif
}
bool validTemplatePath(const std::wstring& path) {
    if (path.empty() || path.size() > 500 || path.find(L'\0') != std::wstring::npos) return false;
    if (path.starts_with(L"asset:")) return portableTemplateToken(path);
    try {
        const std::filesystem::path candidate(path);
        if (!candidate.is_absolute()) return false;
        const auto ext = folded(candidate.extension().wstring());
        return ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp";
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}
bool parseAnchor(const std::wstring& anchor, int& x, int& y) {
    if (!anchor.starts_with(L"v1:")) return false;
    const auto comma = anchor.find(L',', 3);
    if (comma == std::wstring::npos || anchor.find(L',', comma + 1) != std::wstring::npos)
        return false;
    return digitParse(anchor.substr(3, comma - 3), x) &&
           digitParse(anchor.substr(comma + 1), y);
}
std::wstring makeAnchor(int cx, int cy, int width, int height) {
    if (width <= 0 || height <= 0 || cx < 0 || cy < 0 || cx >= width || cy >= height) return {};
    return L"v1:" + std::to_wstring(std::clamp(cx * 10000LL / width, 0LL, 10000LL)) +
           L"," + std::to_wstring(std::clamp(cy * 10000LL / height, 0LL, 10000LL));
}
Result search(const clicknova::platform::WindowTarget& window, const Query& query) {
    Result result;
    if (!validQuery(query)) return {{}, "Invalid visual search query."};
    if (!query.visualTemplate.empty() && !validTemplatePath(query.visualTemplate))
        return {{}, "Select an absolute PNG/JPEG/BMP template file."};
    if (!query.visualTemplate.empty() && !imageAvailable())
        return {{}, "OpenCV support is not enabled in this build."};
#if defined(_WIN32) && defined(CLICKNOVA_WITH_OPENCV)
    if (!query.visualTemplate.empty()) {
        if (const auto problem = templateError(query.visualTemplate); !problem.empty())
            return {{}, problem};
    }
#endif
    if (query.visualOcr && !ocrAvailable())
        return {{}, "Tesseract OCR support is not enabled in this build."};
#if defined(_WIN32) && defined(CLICKNOVA_WITH_OPENCV)
    TargetFrame frame;
    if (!capture(window, frame)) return {{}, "Focus the target window to capture its visible client area."};
    auto append = [&](Provider provider, const std::wstring& selector) {
        std::vector<std::wstring> labels;
        std::string recognitionError;
        auto items = recognize(frame.bgr, provider, selector,
                               static_cast<int>(std::min<std::size_t>(query.limit + 1, 201)),
                               labels, &recognitionError);
        if (!recognitionError.empty() && result.error.empty())
            result.error = std::move(recognitionError);
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (result.elements.size() >= query.limit) { result.truncated = true; break; }
            const auto& m = items[i];
            if (query.role != Role::Any && query.role != Role::Button) continue;
            Element item;
            item.name = labels[i];
            item.automationId = selector;
            item.className = makeAnchor(m.rect.x + m.rect.width / 2,
                                        m.rect.y + m.rect.height / 2,
                                        frame.bgr.cols, frame.bgr.rows);
            item.role = Role::Button;
            item.localizedRole = provider == Provider::Visual ? L"Image match" : L"OCR text";
            item.enabled = true;
            item.provider = provider;
            result.elements.push_back(std::move(item));
        }
    };
    if (!query.visualTemplate.empty())
        append(Provider::Visual, makeSelector(Provider::Visual, query.visualTemplate,
                                             query.imageConfidence, query.visualRadius));
    if (query.visualOcr && !query.text.empty())
        append(Provider::Ocr, makeSelector(Provider::Ocr, query.text,
                                           query.ocrConfidence, query.visualRadius));
    if (!result.elements.empty()) result.error.clear();
#else
    (void)window;
#endif
    return result;
}
bool present(const clicknova::platform::WindowTarget& window, Provider provider,
           const std::wstring& name, const std::wstring& selector,
           const std::wstring& anchor) {
    int x = 0, y = 0, confidence = 0, radiusPercent = 0;
    std::wstring payload;
    if (!parseAnchor(anchor, x, y) ||
        !parseSelector(provider, selector, payload, confidence, radiusPercent)) return false;
#if defined(_WIN32) && defined(CLICKNOVA_WITH_OPENCV)
    if ((provider == Provider::Visual && !imageAvailable()) ||
        (provider == Provider::Ocr && !ocrAvailable())) return false;
    TargetFrame frame;
    if (!capture(window, frame)) return false;
    std::vector<std::wstring> labels;
    const auto found = recognize(frame.bgr, provider, selector, 100, labels);
    (void)name;
    double nearest = 1e30;
    int plausible = 0;
    for (const auto& item : found) {
        const auto& rect = item.rect;
        const auto dx = (rect.x + rect.width * .5) * 10000.0 / frame.bgr.cols - x;
        const auto dy = (rect.y + rect.height * .5) * 10000.0 / frame.bgr.rows - y;
        const auto distance = std::hypot(dx, dy);
        if (distance <= radiusPercent * 100.0) {
            ++plausible;
            nearest = std::min(nearest, distance);
        }
    }
    return plausible == 1 && nearest <= radiusPercent * 100.0 &&
        clicknova::platform::checkTarget(window) == clicknova::platform::TargetCheck::Ready;
#else
    (void)window; (void)name;
    return false;
#endif
}

std::optional<ScreenPoint> locate(const clicknova::platform::WindowTarget& window, Provider provider,
           const std::wstring& name, const std::wstring& selector,
           const std::wstring& anchor) {
    int x = 0, y = 0, confidence = 0, radiusPercent = 0;
    std::wstring payload;
    if (!parseAnchor(anchor, x, y) ||
        !parseSelector(provider, selector, payload, confidence, radiusPercent)) return std::nullopt;
#if defined(_WIN32) && defined(CLICKNOVA_WITH_OPENCV)
    if ((provider == Provider::Visual && !imageAvailable()) ||
        (provider == Provider::Ocr && !ocrAvailable())) return std::nullopt;
    TargetFrame frame;
    if (!capture(window, frame)) return std::nullopt;
    std::vector<std::wstring> labels;
    const auto found = recognize(frame.bgr, provider, selector, 100, labels);
    // The selected anchor is a disambiguator, never a stored click point.
    // Accept only one close match, and refuse near-equal competing matches.
    int unique = -1;
    double bestDistance = 1e30;
    double secondDistance = 1e30;
    for (std::size_t i = 0; i < found.size(); ++i) {
        // OCR is matched by the original requested text in recognize().
        // The full line may change (e.g. score or counter) while the
        // intended substring remains intact and the anchor stays unique.
        (void)name;
        const auto& r = found[i].rect;
        const double nx = (r.x + r.width * .5) * 10000.0 / frame.bgr.cols;
        const double ny = (r.y + r.height * .5) * 10000.0 / frame.bgr.rows;
        const double d = std::hypot(nx - x, ny - y);
        if (d < bestDistance) { secondDistance = bestDistance; bestDistance = d; unique = static_cast<int>(i); }
        else secondDistance = std::min(secondDistance, d);
    }
    // The radius and confidence are stored per binding, not hard-coded per game.
    // More than one plausible candidate inside that radius is ambiguous.
    const double maxDistance = radiusPercent * 100.0;
    if (unique < 0 || bestDistance > maxDistance || secondDistance <= maxDistance) return std::nullopt;
    const auto& r = found[static_cast<std::size_t>(unique)].rect;
    const POINT point{frame.origin.x + r.x + r.width / 2,
                      frame.origin.y + r.y + r.height / 2};
    if (clicknova::platform::checkScreenPoint(window, point.x, point.y) !=
        clicknova::platform::TargetCheck::Ready) return std::nullopt;
    return ScreenPoint{point.x, point.y};
#else
    (void)window; (void)provider; (void)name; (void)selector; (void)anchor;
    return std::nullopt;
#endif
}

bool click(const clicknova::platform::WindowTarget& window, Provider provider,
           const std::wstring& name, const std::wstring& selector,
           const std::wstring& anchor) {
#if defined(_WIN32) && defined(CLICKNOVA_WITH_OPENCV)
    const auto target = locate(window, provider, name, selector, anchor);
    if (!target) return false;
    const POINT point{target->x, target->y};
    // Input goes only to the foreground; the selected HWND/PID and actual
    // hit-tested client pixel were checked just before dispatch.
    const int desktopWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int desktopHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    const int desktopLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int desktopTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    if (desktopWidth <= 1 || desktopHeight <= 1) return false;
    INPUT inputs[3]{};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dx = MulDiv(point.x - desktopLeft, 65535, desktopWidth - 1);
    inputs[0].mi.dy = MulDiv(point.y - desktopTop, 65535, desktopHeight - 1);
    inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[2].type = INPUT_MOUSE;
    inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    if (clicknova::platform::checkScreenPoint(window, point.x, point.y) !=
        clicknova::platform::TargetCheck::Ready) return false;
    if (SendInput(3, inputs, sizeof(INPUT)) == 3) return true;
    // If a partial dispatch sent LEFTDOWN, avoid leaving the button held.
    SendInput(1, &inputs[2], sizeof(INPUT));
    return false;
#else
    (void)window; (void)provider; (void)name; (void)selector; (void)anchor;
    return false;
#endif
}
} // namespace clicknova::accessibility::visual
