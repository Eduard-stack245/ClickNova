// SPDX-License-Identifier: MIT
#include "clicknova/visual_worker_protocol.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace clicknova::visual::worker;
#define REQUIRE(condition) do { if(!(condition)){ std::cerr << "failed: " #condition << '\n'; return 1; } } while(false)
int main(){
    Request req{Provider::Ocr,77,0x112233,912,"some UTF-8 phrase"};
    std::vector<std::uint8_t> bytes,framed,payload;
    REQUIRE(encode(req,bytes));REQUIRE(frame(bytes,framed));
    REQUIRE(unframe(framed,payload));Request restored;
    REQUIRE(decode(payload,restored)&&restored.nonce==77&&restored.query==req.query);
    auto corrupt=payload;corrupt.push_back(0);REQUIRE(!decode(corrupt,restored));
    REQUIRE(!safeTemplateId("../secrets"));REQUIRE(!safeTemplateId("C:evil"));
    REQUIRE(!safeOcrQuery("\xC0\xAF"));
    REQUIRE(safeOcrQuery("Україна"));
    REQUIRE(!safeOcrQuery("\x1b[31m"));
    REQUIRE(!safeOcrQuery(""));
    REQUIRE(!valid(Request{Provider::Image,1,123,456,"../secret"}));
    REQUIRE(valid(Request{Provider::Image,1,123,456,"logo_01"}));
    Reply rep{Provider::Ocr,77,Status::Found,1};
    REQUIRE(encode(rep,bytes));Reply result;
    REQUIRE(decode(bytes,result)&&matches(req,result));
    result.nonce++;REQUIRE(!matches(req,result));
    result.nonce--;result.provider=Provider::Image;REQUIRE(!matches(req,result));
    result=Reply{Provider::Ocr,77,Status::NotFound,1};REQUIRE(!valid(result));
    REQUIRE(!unframe({framed.data(),framed.size()-1},payload));
    std::cout<<"Visual preview protocol contract OK\n";
    return 0;
}
