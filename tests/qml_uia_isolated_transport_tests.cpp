// SPDX-License-Identifier: MIT
#include "../src/gui/qml/uia_isolated_transport.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QFileInfo>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stop_token>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#define CHECK(x) do {if(!(x)){std::cerr<<"failed line "<<__LINE__<<" "<<#x<<'\n';std::abort();}}while(false)
using namespace clicknova::accessibility;
using namespace clicknova::gui::qml::uia_transport;
static int fixture(const QString& mode){
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    if(mode==QStringLiteral("--hang")){QThread::msleep(4000);return 0;}
    unsigned char prefix[4]{};
    if(!std::cin.read(reinterpret_cast<char*>(prefix),4))return 3;
    const std::uint32_t n=std::uint32_t(prefix[0]) | (std::uint32_t(prefix[1])<<8)|
        (std::uint32_t(prefix[2])<<16)|(std::uint32_t(prefix[3])<<24);
    if(n==0||n>ipc::maxFrame)return 4;
    std::vector<std::uint8_t> b(n);
    if(!std::cin.read(reinterpret_cast<char*>(b.data()),n))return 5;
    ipc::Request request;
    if(!ipc::decode(b,request))return 6;
    if(mode==QStringLiteral("--fail"))return 7;
    if(mode==QStringLiteral("--malformed")){
        std::cout<<"not-an-ipc-response";return 0;
    }
    if(mode==QStringLiteral("--slow"))QThread::msleep(4000);
    // BuildFix2: construct a valid response before applying negative-case mutations.
    ipc::Reply reply;
    reply.operation = request.operation;
    reply.provider = request.provider;
    reply.nonce = request.nonce;
    reply.scan = {candidates::Status::Complete, {{50000,true,"private","Button"}}, 1, false};
    reply.verification = {candidates::VerifyStatus::Verified, request.candidate};

    if (mode == QStringLiteral("--nonce")) ++reply.nonce;
    if (mode == QStringLiteral("--wrong-operation")) {
        reply.operation = request.operation == ipc::Operation::Scan
            ? ipc::Operation::Verify : ipc::Operation::Scan;
        // A valid Verify/Failed reply has no candidate payload. The decoder
        // accepts it, but matchesRequest rejects the mismatched operation.
        if (reply.operation == ipc::Operation::Verify)
            reply.verification = {candidates::VerifyStatus::Failed, {}};
        else
            reply.scan = {candidates::Status::Complete, {}, 0, false};
    }
    if (mode == QStringLiteral("--overscan"))
        reply.scan.visited = request.limits.maxVisited + 1;
    if (mode == QStringLiteral("--overitems")) {
        reply.scan.items.push_back({50000,true,"second","Button"});
        reply.scan.visited = 2;
    }
    if (mode == QStringLiteral("--wrong-identity") && request.operation == ipc::Operation::Verify)
        reply.verification.match.automationId = "different";
    if (mode == QStringLiteral("--wrong-provider") && request.operation == ipc::Operation::Scan) {
        reply.provider = ipc::Provider::Msaa;
        reply.scan.items.clear();
        reply.scan.visited = 0;
    }
    std::vector<std::uint8_t> encoded,framed;
    if(!ipc::encode(reply,encoded)||!ipc::frame(encoded,framed))return 8;
    std::cout.write(reinterpret_cast<const char*>(framed.data()),static_cast<std::streamsize>(framed.size()));
    return std::cout.good()?0:9;
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    if(argc==2)return fixture(QString::fromLocal8Bit(argv[1]));
    ipc::Request r;
    r.nonce=56;r.target={123,456};
    const QString exe=QCoreApplication::applicationFilePath();
    CHECK(QFileInfo(exe).isAbsolute());
    const auto ok=transact(exe,{QStringLiteral("--valid")},r,1600,{});
    CHECK(ok.outcome==Outcome::Success);
    CHECK(ok.reply.scan.items.size()==1&&ok.reply.scan.items[0].automationId=="private");
    CHECK(transact(exe,{QStringLiteral("--nonce")},r,1600,{}).outcome==Outcome::Malformed);
    CHECK(transact(exe,{QStringLiteral("--wrong-operation")},r,1600,{}).outcome==Outcome::Malformed);
    CHECK(transact(exe,{QStringLiteral("--wrong-provider")},r,1600,{}).outcome==Outcome::Malformed);
    CHECK(transact(exe,{QStringLiteral("--overscan")},r,1600,{}).outcome==Outcome::Malformed);
    r.limits.maxCandidates=1;
    CHECK(transact(exe,{QStringLiteral("--overitems")},r,1600,{}).outcome==Outcome::Malformed);
    r.limits.maxCandidates=24;
    CHECK(transact(exe,{QStringLiteral("--fail")},r,1600,{}).outcome==Outcome::Unavailable);
    CHECK(transact(exe,{QStringLiteral("--malformed")},r,1600,{}).outcome==Outcome::Malformed);
    QElapsedTimer clock;clock.start();
    CHECK(transact(exe,{QStringLiteral("--hang")},r,300,{}).outcome==Outcome::TimedOut);
    CHECK(clock.elapsed()<2200);
    std::stop_source stop;
    std::atomic<bool> cancelled{false};
    std::jthread task([&](std::stop_token){
        cancelled=transact(exe,{QStringLiteral("--slow")},r,7000,stop.get_token()).outcome==Outcome::Cancelled;
    });
    QThread::msleep(110);stop.request_stop();task.join();
    CHECK(cancelled);
    r.operation=ipc::Operation::Verify;r.candidate={50000,true,"private","Button"};
    const auto bound=transact(exe,{QStringLiteral("--valid")},r,1600,{});
    CHECK(bound.outcome==Outcome::Success);
    CHECK(bound.reply.verification.match.automationId=="private");
    CHECK(transact(exe,{QStringLiteral("--wrong-identity")},r,1600,{}).outcome==Outcome::Malformed);
    std::cout<<"PASS isolated UIA worker transport: real process, timeout, cancel, no fallback\n";
}
