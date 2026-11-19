// SPDX-License-Identifier: MIT
#include "clicknova/uia_worker_protocol.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::cerr << "Java IPC FAIL line " << __LINE__ << ": " << #x << '\n'; std::abort(); } } while(false)
namespace ipc = clicknova::accessibility::ipc;
namespace ca = clicknova::accessibility::candidates;
int main() {
    ipc::Request query;
    query.provider = ipc::Provider::Java;
    query.operation = ipc::Operation::Scan;
    query.nonce = 0x91820345;
    query.target = {0x1234, 123};
    query.limits = {320, 24, 6, 1200};
    std::vector<std::uint8_t> encoded;
    CHECK(ipc::encode(query, encoded));
    ipc::Request decodedRequest;
    CHECK(ipc::decode(encoded, decodedRequest));
    CHECK(decodedRequest.provider == ipc::Provider::Java);
    ipc::Reply result;
    result.provider = ipc::Provider::Java;
    result.operation = ipc::Operation::Scan;
    result.nonce = query.nonce;
    result.scan = {ca::Status::Complete, {{50000,true,"",""}, {50011,false,"",""}}, 3, false};
    CHECK(ipc::encode(result, encoded));
    ipc::Reply decoded;
    CHECK(ipc::decode(encoded, decoded));
    CHECK(ipc::matchesRequest(query, decoded));
    decoded.provider = ipc::Provider::Uia;
    CHECK(!ipc::matchesRequest(query, decoded));
    decoded.provider = ipc::Provider::Java;
    decoded.scan.items[0].automationId = "SECRET";
    CHECK(!ipc::matchesRequest(query, decoded));
    CHECK(!ipc::encode(decoded, encoded));
    decoded.scan.items[0].automationId.clear();
    decoded.scan.items[1].className = "SECRET";
    CHECK(!ipc::matchesRequest(query, decoded));
    CHECK(!ipc::encode(decoded, encoded));
    result.scan.items[0].automationId = "never";
    CHECK(!ipc::encode(result, encoded));
    result.scan.items[0].automationId.clear();
    result.scan.items[0].className = "never";
    CHECK(!ipc::encode(result, encoded));
    result.scan.items[0].className.clear();
    result.scan.items.resize(25);
    result.scan.visited = 30;
    CHECK(ipc::encode(result, encoded)); // protocol max 32
    CHECK(ipc::decode(encoded, decoded));
    CHECK(!ipc::matchesRequest(query, decoded)); // query only allowed 24
    result.scan.items.resize(2);
    result.scan.visited = 1;
    CHECK(ipc::encode(result, encoded));
    CHECK(ipc::decode(encoded, decoded));
    CHECK(!ipc::matchesRequest(query, decoded)); // more results than nodes
    query.operation = ipc::Operation::Verify;
    query.candidate = {50000,true,"id","class"};
    CHECK(!ipc::encode(query, encoded));
    CHECK(!ipc::permitted(ipc::Provider::Java, ipc::Operation::Verify));
    CHECK(!ipc::permitted(ipc::Provider::Msaa, ipc::Operation::Verify));
    CHECK(ipc::permitted(ipc::Provider::Uia, ipc::Operation::Verify));
    // Corrupt frames must not turn Java read-only replies into valid bindings.
    result.scan = {ca::Status::Complete, {{50000,true,"",""}}, 2, false};
    CHECK(ipc::encode(result, encoded));
    std::uint32_t seed = 0x73ab1c8eu;
    for (int i = 0; i < 24000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        auto broken = encoded;
        broken[seed % broken.size()] ^= static_cast<std::uint8_t>(1u << ((seed >> 24) & 7u));
        ipc::Reply trial;
        if (ipc::decode(broken, trial)) {
            CHECK(trial.operation != ipc::Operation::Verify ||
                  trial.provider != ipc::Provider::Java);
            if (trial.provider == ipc::Provider::Java) {
                for (const auto& c : trial.scan.items) {
                    CHECK(c.automationId.empty() && c.className.empty());
                }
            }
        }
    }
    std::cout << "PASS Java IPC provider isolation/limits/privacy, 24000 mutation cases\n";
}
