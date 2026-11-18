// SPDX-License-Identifier: MIT
#include "clicknova/verified_uia_binding.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#define CHECK(x) do { if(!(x)) {std::cerr << "FAIL " << __LINE__ << " " #x << '\n';std::abort();} }while(false)
using namespace clicknova;
int main() {
    using namespace accessibility::candidates;
    using namespace accessibility::binding;
    using namespace input;
    CHECK(supportedRole(50000)==1 && supportedRole(50005)==4 && supportedRole(50011)==7);
    CHECK(supportedRole(50004)==0);
    Candidate good{50000,true,"run-button","Button"};
    CHECK(eligible(good));
    CHECK(!eligible({50000,false,"id","Button"}));
    CHECK(!eligible({50000,true,"","Button"}));
    CHECK(!eligible({50000,true,"id",""}));
    CHECK(!eligible({50004,true,"id","Edit"}));
    CHECK(!eligible({50000,true,"bad\n", "Button"}));
    CHECK(!eligible({50000,true,std::string(385,'X'),"Button"}));
    CHECK(!sameIdentity(good,{50000,true,"other","Button"}));
    CHECK(sameIdentity(good,{50000,false,"run-button","Button"}));
    StepWindow binding;
    binding.enforce=true;
    binding.title="Window"; binding.process="app.exe";
    binding.manualGroupId="group";
    binding.manualGroupTitle="Important";
    OriginalWindow original{"Window","app.exe"};
    Action click=Click{Button::Left,1,200};
    CHECK(canCommit(click,binding,original,good));
    CHECK(!canCommit(Click{Button::Left,2,200},binding,original,good));
    CHECK(!canCommit(Click{Button::Right,1,200},binding,original,good));
    CHECK(!canCommit(Wait{100},binding,original,good));
    CHECK(!canCommit(click,binding,{"Wrong","app.exe"},good));
    CHECK(!canCommit(click,binding,{"Window","evil.exe"},good));
    binding.enforce=false;CHECK(!canCommit(click,binding,original,good));
    binding.enforce=true;
    CHECK(assign(binding,good));
    CHECK(binding.elementName.empty());
    CHECK(binding.elementAutomationId=="run-button" && binding.elementClass=="Button");
    CHECK(binding.elementProvider==0 && binding.elementRole==1 && binding.elementOperation==0);
    CHECK(binding.manualGroupId=="group" && binding.title=="Window" && binding.process=="app.exe");
    CHECK(!assign(binding,{50004,true,"id","Edit"}));
    CHECK(binding.elementAutomationId=="run-button");
    std::cout << "PASS verified binding policy and model compatibility\n";
}
