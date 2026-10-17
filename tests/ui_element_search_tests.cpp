// SPDX-License-Identifier: Apache-2.0
#include "clicknova/ui_element_search.hpp"
#include <cassert>

int main() {
    using namespace clicknova::accessibility;
    assert(validQuery({L"OK", Role::Button, false, 10}));
    assert(!Query{}.includeAnonymous);
    assert(validQuery({L"", Role::Any, false, 10, true}));
    assert(validQuery({L"", Role::Any, true, 200}));
    assert(!validQuery({L"", Role::Any, false, 0}));
    assert(validQuery({L"", Role::Any, false, 400}));
    assert(!validQuery({L"", Role::Any, false, 601}));
    Query tabQuery{L"", Role::Any, true, 400};
    tabQuery.browserTab = L"Example";
    assert(validQuery(tabQuery));
    tabQuery.browserOnly = false;
    assert(!validQuery(tabQuery));
    assert(!validQuery({std::wstring(513, L'x'), Role::Any, false, 10}));
    assert(!validQuery({std::wstring(L"hi\0there", 8), Role::Any, false, 10}));
    assert(!validQuery({L"", static_cast<Role>(99), false, 10}));
    assert(browserExecutable(L"C:\\Program Files\\Google\\Chrome\\CHROME.EXE"));
    assert(browserExecutable(L"msedge.exe"));
    assert(browserExecutable(L"firefox.exe"));
    assert(browserExecutable(L"chromium.exe"));
    assert(browserExecutable(L"Arc.exe"));
    assert(!browserExecutable(L"notepad.exe"));
#ifndef _WIN32
    assert(listBrowserTabs({}).empty());
#endif
    assert(!selectBrowserTab({}, L"Example"));
    assert(!selectBrowserTab({}, L""));
    assert(!search({}, Query{L"test", Role::Button, true, 10}).error.empty());
    using Provider = clicknova::accessibility::Provider;
    assert(std::string(providerName(Provider::Uia)) == "UIA");
    assert(std::string(providerName(Provider::Msaa)) == "MSAA");
    assert(std::string(providerName(Provider::Java)) == "Java Access Bridge");
    assert(std::string(providerName(Provider::Visual)) == "OpenCV");
    assert(std::string(providerName(Provider::Ocr)) == "OCR");
    assert(supportsProvider(Operation::Invoke, Role::Button, Provider::Visual));
    assert(supportsProvider(Operation::Invoke, Role::Button, Provider::Ocr));
    assert(!supportsProvider(Operation::SetValue, Role::Edit, Provider::Visual));
    Query visualSearch{L"PLAY", Role::Any, false, 20};
    visualSearch.visualOcr = true;
    assert(validQuery(visualSearch));
    visualSearch.text.clear();
    assert(!validQuery(visualSearch));
    assert(supportsProvider(Operation::Invoke, Role::Button, Provider::Msaa));
    assert(supportsProvider(Operation::Focus, Role::Any, Provider::Java));
    assert(supportsProvider(Operation::SetValue, Role::Edit, Provider::Java));
    assert(!supportsProvider(Operation::Check, Role::CheckBox, Provider::Java));
    assert(!supportsProvider(Operation::Expand, Role::ComboBox, Provider::Msaa));
    assert(!supportsProvider(Operation::Invoke, Role::Edit, Provider::Java));
    assert(!supportsProvider(Operation::Invoke, Role::Button, static_cast<Provider>(99)));
    assert(!perform({}, L"OK", L"msaa/d0", L"", Role::Button, false,
                    Operation::Invoke, L"", Provider::Msaa));
    assert(!perform({}, L"OK", L"jab/0", L"", Role::Button, false,
                    Operation::Invoke, L"", Provider::Java));
    assert(!perform({}, L"OK", L"jab/0", L"", Role::Button, false,
                    Operation::Invoke, L"", Provider::Java, true));
    assert(!perform({}, L"Button", L"ocr:Play", L"v1:5000,5000", Role::Button,
                    false, Operation::Invoke, L"", Provider::Ocr, true));
    assert(!perform({}, L"Button", L"", L"", Role::Button,
                    true, Operation::Invoke, L"", Provider::Uia, true));

    using Op = Operation;
    assert(supports(Op::Invoke, Role::Button));
    assert(supports(Op::Invoke, Role::Link));
    assert(!supports(Op::Invoke, Role::Edit));
    assert(supports(Op::SetValue, Role::Edit));
    assert(supports(Op::SetValue, Role::ComboBox));
    assert(supports(Op::Check, Role::CheckBox));
    assert(supports(Op::Uncheck, Role::CheckBox));
    assert(supports(Op::Select, Role::ListItem));
    assert(supports(Op::Select, Role::RadioButton));
    assert(supports(Op::Expand, Role::ComboBox));
    assert(supports(Op::Collapse, Role::TreeItem));
    assert(supports(Op::SetRangeValue, Role::Slider));
    assert(supports(Op::ScrollIntoView, Role::Any));
    assert(supports(Op::Focus, Role::Edit));
    assert(!supports(static_cast<Op>(100), Role::Button));
    assert(validValue(Op::SetValue, "")); // clear field
    assert(validValue(Op::SetValue, "Hello"));
    assert(validValue(Op::SetRangeValue, "-4.25"));
    assert(!validValue(Op::SetRangeValue, "nan"));
    assert(!validValue(Op::SetRangeValue, "1,25"));
    assert(!validValue(Op::SetRangeValue, ""));
    assert(!validValue(Op::Check, "unexpected"));
    assert(!validValue(Op::SetValue, std::string(4097, 'a')));
    // No selected target: NEVER act on an arbitrary desktop control.
    assert(!perform({}, L"Field", L"", L"", Role::Edit, false, Op::SetValue, L"hello"));
    assert(!perform({}, L"Check", L"", L"", Role::CheckBox, false, Op::Check, L""));
    assert(!perform({}, L"Slider", L"", L"", Role::Slider, false, Op::SetRangeValue, L"12"));
    // A missing or stale target must fail closed; never invoke an arbitrary UI element.
    assert(!invoke({}, L"OK", L"", L"", Role::Button, false));
}
