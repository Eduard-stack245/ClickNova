// SPDX-License-Identifier: MIT
#include "clicknova/uia_worker_protocol.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>
#define CHECK(x) do { if(!(x)){std::cerr<<"failed at "<<__LINE__<<": "<<#x<<'\n';std::abort();}}while(false)
using namespace clicknova::accessibility;
int main(){
    ipc::Request req;req.nonce=0x123456789123ULL;req.target={0x1234u,222};
    std::vector<std::uint8_t> bytes,framed;
    CHECK(ipc::encode(req,bytes));CHECK(ipc::frame(bytes,framed));
    std::vector<std::uint8_t> stripped;
    CHECK(ipc::unframe(framed,stripped)&&stripped==bytes);
    ipc::Request parsed;
    CHECK(ipc::decode(stripped,parsed));
    CHECK(parsed.nonce==req.nonce&&parsed.target.processId==222);
    CHECK(parsed.operation==ipc::Operation::Scan);
    auto bad=bytes;bad.push_back(0);
    CHECK(!ipc::decode(bad,parsed));
    bad=bytes;bad[0]=0;CHECK(!ipc::decode(bad,parsed));
    bad=bytes;bad[8]=255;CHECK(!ipc::decode(bad,parsed));
    bad=framed;bad[0]=255;CHECK(!ipc::unframe(bad,stripped));
    bad=framed;bad.pop_back();CHECK(!ipc::unframe(bad,stripped));
    req.operation=ipc::Operation::Verify;
    CHECK(!ipc::encode(req,bytes));
    req.candidate={50000,true,"secret-123","Button"};
    CHECK(ipc::encode(req,bytes));CHECK(ipc::decode(bytes,parsed));
    CHECK(parsed.candidate.automationId=="secret-123");
    bad=bytes;bad.pop_back();CHECK(!ipc::decode(bad,parsed));
    req.candidate.automationId=std::string(500,'x');CHECK(!ipc::encode(req,bytes));
    req.candidate.automationId="\xc0\xaf"; // overlong UTF-8 rejected
    CHECK(!ipc::encode(req,bytes));
    req.candidate.automationId="id";
    req.candidate.className="Button";
    req.candidate.enabled=false;CHECK(!ipc::encode(req,bytes));
    ipc::Reply answer;answer.operation=ipc::Operation::Scan;
    answer.nonce=req.nonce;
    answer.scan={candidates::Status::Complete,
                 {{50000,true,"secret-123","Button"},{50005,false,"",""}},2,false};
    CHECK(ipc::encode(answer,bytes));
    ipc::Reply decoded;
    CHECK(ipc::decode(bytes,decoded));
    CHECK(decoded.scan.items.size()==2);
    CHECK(decoded.scan.items[0].automationId=="secret-123");
    CHECK(!decoded.scan.items[1].enabled);
    bad=bytes;bad.push_back(42);CHECK(!ipc::decode(bad,decoded));
    answer.scan.items.resize(33);CHECK(!ipc::encode(answer,bytes));
    answer.scan.items.clear();
    answer.scan.status=candidates::Status::Failed;
    CHECK(ipc::encode(answer,bytes));CHECK(ipc::decode(bytes,decoded));
    answer.scan.items.push_back({50000,true,"abc","xyz"});
    CHECK(ipc::encode(answer,bytes));CHECK(!ipc::decode(bytes,decoded));
    answer.operation=ipc::Operation::Verify;
    answer.verification={candidates::VerifyStatus::Verified,{50000,true,"secret-123","Button"}};
    CHECK(ipc::encode(answer,bytes));CHECK(ipc::decode(bytes,decoded));
    CHECK(decoded.verification.status==candidates::VerifyStatus::Verified);
    CHECK(decoded.verification.match.automationId=="secret-123");
    answer.verification={candidates::VerifyStatus::Ambiguous,{}};
    CHECK(ipc::encode(answer,bytes));CHECK(ipc::decode(bytes,decoded));
    CHECK(decoded.verification.status==candidates::VerifyStatus::Ambiguous);
    bad=bytes;bad[24]=255;CHECK(!ipc::decode(bad,decoded));
    // A correctly encoded reply must still obey the specific request. In
    // particular, the worker may NOT inflate the scan or replace a candidate.
    ipc::Request admission;
    admission.nonce=1234;
    admission.target={0x1234u, 222};
    admission.limits={10,1,2,500};
    ipc::Reply admissionReply;
    admissionReply.operation=ipc::Operation::Scan;
    admissionReply.nonce=admission.nonce;
    admissionReply.scan={candidates::Status::Complete,
        {{50000,true,"id","Button"}},1,false};
    CHECK(ipc::matchesRequest(admission,admissionReply));
    admissionReply.scan.visited=11;
    CHECK(!ipc::matchesRequest(admission,admissionReply));
    admissionReply.scan.visited=0;
    CHECK(!ipc::matchesRequest(admission,admissionReply));
    admissionReply.scan.visited=1;
    admissionReply.scan.items.push_back({50000,true,"id2","Button"});
    CHECK(!ipc::matchesRequest(admission,admissionReply));
    admissionReply.scan.items.resize(1);
    admissionReply.nonce++;
    CHECK(!ipc::matchesRequest(admission,admissionReply));
    admissionReply.nonce=admission.nonce;
    admissionReply.operation=ipc::Operation::Verify;
    CHECK(!ipc::matchesRequest(admission,admissionReply));
    admission.operation=ipc::Operation::Verify;
    admission.candidate={50000,true,"id","Button"};
    admissionReply.verification={candidates::VerifyStatus::Verified,admission.candidate};
    CHECK(ipc::matchesRequest(admission,admissionReply));
    admissionReply.verification.match.automationId="another";
    CHECK(!ipc::matchesRequest(admission,admissionReply));
    admissionReply.verification={candidates::VerifyStatus::Ambiguous,{}};
    CHECK(ipc::matchesRequest(admission,admissionReply));
    // Phase21: provider provenance is part of both versioned IPC envelopes.
    ipc::Request msaa;msaa.operation=ipc::Operation::Scan;
    msaa.provider=ipc::Provider::Msaa;msaa.nonce=67;msaa.target={123,456};
    CHECK(ipc::encode(msaa,bytes));CHECK(ipc::decode(bytes,parsed));
    CHECK(parsed.provider==ipc::Provider::Msaa);
    ipc::Reply msaaAnswer;msaaAnswer.provider=ipc::Provider::Msaa;
    msaaAnswer.operation=ipc::Operation::Scan;msaaAnswer.nonce=msaa.nonce;
    msaaAnswer.scan={candidates::Status::Complete,{{50000,true,"",""}},1,false};
    CHECK(ipc::encode(msaaAnswer,bytes));CHECK(ipc::decode(bytes,decoded));
    CHECK(ipc::matchesRequest(msaa,decoded));
    decoded.provider=ipc::Provider::Uia;
    CHECK(!ipc::matchesRequest(msaa,decoded)); // provider substitution
    msaaAnswer.scan.items[0].automationId="private";
    CHECK(!ipc::encode(msaaAnswer,bytes)); // data leak from MSAA rejected
    msaa.operation=ipc::Operation::Verify;
    msaa.candidate={50000,true,"id","class"};
    CHECK(!ipc::encode(msaa,bytes)); // MSAA bind forbidden
    ipc::Request java;java.operation=ipc::Operation::Scan;
    java.provider=ipc::Provider::Java;java.nonce=68;java.target={123,456};
    CHECK(ipc::encode(java,bytes));CHECK(ipc::decode(bytes,parsed));
    ipc::Reply javaAnswer;javaAnswer.operation=ipc::Operation::Scan;
    javaAnswer.provider=ipc::Provider::Java;javaAnswer.nonce=java.nonce;
    javaAnswer.scan.status=candidates::Status::Unavailable;
    CHECK(ipc::encode(javaAnswer,bytes));CHECK(ipc::decode(bytes,decoded));
    CHECK(ipc::matchesRequest(java,decoded));
    javaAnswer.scan.status=candidates::Status::Complete;
    javaAnswer.scan.visited=1;
    javaAnswer.scan.items={{50000,true,"",""}};
    CHECK(ipc::encode(javaAnswer,bytes));
    CHECK(ipc::decode(bytes,decoded));
    CHECK(ipc::matchesRequest(java,decoded));
    javaAnswer.scan.items[0].className="private";
    CHECK(!ipc::encode(javaAnswer,bytes));
    java.operation=ipc::Operation::Verify;
    java.candidate={50000,true,"id","class"};
    CHECK(!ipc::encode(java,bytes)); // no UIA fallback on unsupported Java
    // Deterministic malformed-data regression: never accept truncated/random frames.
    std::uint32_t seed=0xabc123ef;
    for(int i=0;i<24000;++i){
        seed=seed*1664525u+1013904223u;
        auto fuzz=bytes;
        const std::size_t pos=seed%fuzz.size();
        fuzz[pos]=static_cast<std::uint8_t>(seed>>16);
        if(seed&1) fuzz.resize((seed>>4)%fuzz.size());
        ipc::Reply ignored;
        (void)ipc::decode(fuzz,ignored);
        std::vector<std::uint8_t> ignoredFrame;
        (void)ipc::unframe(fuzz,ignoredFrame);
    }
    std::cout<<"PASS UIA worker protocol round-trip, bounded, fuzzed and fail-closed\n";
}
