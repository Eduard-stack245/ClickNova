// SPDX-License-Identifier: MIT
#include "clicknova/uia_worker_protocol.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>
#define CHECK(x) do {if(!(x)){std::cerr << "FAIL line " << __LINE__ << ": " << #x << '\n';std::abort();}}while(false)
namespace ca = clicknova::accessibility::candidates;
namespace ipc = clicknova::accessibility::ipc;
int main() {
    for(int provider=0;provider<5;++provider) {
        ca::Context context;
        context.expectedRow=context.selectedRow=0;
        context.actionCount=1; context.workflowEditable=context.actionEditable=true;
        context.supportsElementSearch=context.boundWindow=context.targetVerified=true;
        context.provider=provider;
        CHECK((ca::authorize(context)==ca::Refusal::None)==(provider<=2));
    }
    CHECK(ipc::permitted(ipc::Provider::Uia, ipc::Operation::Verify));
    CHECK(ipc::permitted(ipc::Provider::Msaa, ipc::Operation::Scan));
    CHECK(ipc::permitted(ipc::Provider::Java, ipc::Operation::Scan));
    CHECK(!ipc::permitted(ipc::Provider::Msaa, ipc::Operation::Verify));
    CHECK(!ipc::permitted(ipc::Provider::Java, ipc::Operation::Verify));
    ipc::Request request;
    request.provider=ipc::Provider::Msaa;
    request.nonce=7;request.target={42,84};
    std::vector<std::uint8_t> payload;
    CHECK(ipc::encode(request,payload));
    ipc::Request decodedRequest;
    CHECK(ipc::decode(payload,decodedRequest));
    CHECK(decodedRequest.provider==ipc::Provider::Msaa);
    ipc::Reply reply;
    reply.provider=ipc::Provider::Msaa;
    reply.nonce=request.nonce;
    reply.scan.status=ca::Status::Complete;
    reply.scan.visited=2;
    reply.scan.items={{50000,true,"",""},{50011,true,"",""}};
    CHECK(ipc::encode(reply,payload));
    ipc::Reply decodedReply;
    CHECK(ipc::decode(payload,decodedReply));
    CHECK(ipc::matchesRequest(request,decodedReply));
    CHECK(!ca::eligible(decodedReply.scan.items[0]));
    CHECK(!ca::eligible(decodedReply.scan.items[1]));
    for(const auto& c:decodedReply.scan.items) {
        CHECK(c.automationId.empty());
        CHECK(c.className.empty());
    }
    decodedReply.provider=ipc::Provider::Uia;
    CHECK(!ipc::matchesRequest(request,decodedReply));
    decodedReply.provider=ipc::Provider::Msaa;
    decodedReply.scan.items[0].className="leak";
    CHECK(!ipc::matchesRequest(request,decodedReply));
    CHECK(!ipc::encode(decodedReply,payload));
    // UIA worker replies may NOT be spoofed as Java or MSAA.
    request.provider=ipc::Provider::Java;
    reply.provider=ipc::Provider::Java;
    reply.scan={ca::Status::Unavailable,{},0,false};
    CHECK(ipc::encode(reply,payload));
    CHECK(ipc::decode(payload,decodedReply));
    reply.nonce=request.nonce;
    CHECK(ipc::encode(reply,payload));
    CHECK(ipc::decode(payload,decodedReply));
    CHECK(ipc::matchesRequest(request,decodedReply));
    decodedReply.scan.status=ca::Status::Complete;
    CHECK(ipc::matchesRequest(request,decodedReply)); // Java read-only is supported by phase22
    decodedReply.scan.visited=1;
    decodedReply.scan.items={{50000,true,"",""}};
    CHECK(ipc::matchesRequest(request,decodedReply));
    decodedReply.scan.items[0].automationId="private";
    CHECK(!ipc::matchesRequest(request,decodedReply));
    // Protocol v1 (missing the provider field) must not be accepted as v2.
    std::vector<std::uint8_t> v1;
    request.provider=ipc::Provider::Uia;
    CHECK(ipc::encode(request,v1));
    v1[4]=1; // old version
    CHECK(!ipc::decode(v1,decodedRequest));
    std::cout << "PASS: MSAA/Java read-only, provider provenance, privacy, no binding\n";
}
