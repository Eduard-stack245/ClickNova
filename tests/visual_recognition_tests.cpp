// SPDX-License-Identifier: Apache-2.0
#include "clicknova/visual_recognition.hpp"
#include <cassert>
#include <string>
int main() {
    using namespace clicknova::accessibility::visual;
    int x = -1, y = -1;
    assert(parseAnchor(L"v1:1000,9999", x, y) && x == 1000 && y == 9999);
    assert(parseAnchor(L"v1:10000,0", x, y) && x == 10000 && y == 0);
    assert(!parseAnchor(L"v1:10001,1", x, y));
    assert(!parseAnchor(L"v1:1,-1", x, y));
    assert(!parseAnchor(L"v1:1,2,3", x, y));
    assert(!parseAnchor(L"1:1,2", x, y));
    assert(makeAnchor(100, 100, 200, 200) == L"v1:5000,5000");
    assert(makeAnchor(-1, 100, 200, 200).empty());
    assert(!validTemplatePath(L"../template.png"));
    assert(!validTemplatePath(L"relative.jpg"));
    assert(!parseAnchor(L"v1:99999,0", x, y));
    assert(!parseAnchor(L"v1:1,2 ", x, y));
    assert(!validTemplatePath(L""));
    const std::wstring asset = L"asset:" + std::wstring(64, L'a') + L".png";
    assert(validTemplatePath(asset));
    assert(!validTemplatePath(L"asset:../outside.png"));
    assert(!validTemplatePath(L"asset:" + std::wstring(64, L'Z') + L".png"));
    assert(!validTemplatePath(L"asset:" + std::wstring(64, L'a') + L".exe"));
    assert(!validTemplatePath(L"asset:" + std::wstring(64, L'a') + L"/other.png"));
    int portableScore = 0, portableRadius = 0;
    std::wstring portablePayload;
    const auto portableImage = makeSelector(clicknova::accessibility::Provider::Visual,
        asset, 88, 13);
    assert(parseSelector(clicknova::accessibility::Provider::Visual,
        portableImage, portablePayload, portableScore, portableRadius));
    assert(portablePayload == asset && portableScore == 88 && portableRadius == 13);
#ifdef _WIN32
    assert(validTemplatePath(L"C:\\assets\\button.png"));
    assert(!validTemplatePath(L"C:\\assets\\button.exe"));
#else
    assert(validTemplatePath(L"/assets/button.jpg"));
    assert(!validTemplatePath(L"/assets/button.exe"));
#endif
    assert(!ocrAvailable() || imageAvailable());
    using clicknova::accessibility::Provider;
    std::wstring payload;
    int confidence = 0, radius = 0;
#ifdef _WIN32
    const std::wstring path = L"C:\\Assets\\тест.png";
#else
    const std::wstring path = L"/assets/template.png";
#endif
    const auto image = makeSelector(Provider::Visual, path, 93, 12);
    assert(!image.empty());
    assert(parseSelector(Provider::Visual, image, payload, confidence, radius));
    assert(payload == path && confidence == 93 && radius == 12);
    assert(parseSelector(Provider::Visual, L"cv:" + path, payload, confidence, radius));
    assert(confidence == 90 && radius == 8);
    assert(!parseSelector(Provider::Ocr, image, payload, confidence, radius));
    assert(makeSelector(Provider::Visual, path, 100, 12).empty());
    assert(makeSelector(Provider::Visual, path, 93, 31).empty());
    assert(!parseSelector(Provider::Visual, L"cv:@99,0|" + path, payload, confidence, radius));
    assert(!parseSelector(Provider::Visual, L"cv:@foo,8|" + path, payload, confidence, radius));
    const auto textSelector = makeSelector(Provider::Ocr, L"Начать игру", 72, 25);
    assert(parseSelector(Provider::Ocr, textSelector, payload, confidence, radius));
    assert(payload == L"Начать игру" && confidence == 72 && radius == 25);
    assert(parseSelector(Provider::Ocr, L"ocr:Play", payload, confidence, radius));
    assert(confidence == 60 && radius == 8);
}
