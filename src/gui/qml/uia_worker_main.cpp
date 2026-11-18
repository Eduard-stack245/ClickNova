// SPDX-License-Identifier: MIT
#include "uia_worker_main.hpp"
#include "element_candidate_win32.hpp"
#include "element_candidate_msaa_win32.hpp"
#include "element_candidate_java_win32.hpp"
#include "clicknova/uia_worker_protocol.hpp"
#include <cstdio>
#include <iostream>
#include <span>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace clicknova::gui::qml {
int runUiaWorker() noexcept {
#ifndef _WIN32
    return 77;
#else
    using namespace clicknova::accessibility;
    try {
        if(_setmode(_fileno(stdin),_O_BINARY)<0 || _setmode(_fileno(stdout),_O_BINARY)<0)
            return 65;
        unsigned char prefix[4]{};
        if(!std::cin.read(reinterpret_cast<char*>(prefix),4))return 66;
        const std::uint32_t n=std::uint32_t(prefix[0])|
            (std::uint32_t(prefix[1])<<8)|(std::uint32_t(prefix[2])<<16)|
            (std::uint32_t(prefix[3])<<24);
        if(n==0||n>ipc::maxFrame)return 67;
        std::vector<std::uint8_t> bytes(n);
        if(!std::cin.read(reinterpret_cast<char*>(bytes.data()),n))return 68;
        // Exactly one framed request per process. The parent closes stdin
        // after writing; extra bytes or a second frame are rejected.
        if(std::cin.peek() != std::char_traits<char>::eof())return 75;
        ipc::Request request;
        if(!ipc::decode(bytes,request))return 69;
        std::shared_ptr<candidates::IProvider> provider;
        if (request.provider == ipc::Provider::Uia)
            provider = createNativeUiaCandidateProvider();
        else if (request.provider == ipc::Provider::Msaa)
            provider = createNativeMsaaCandidateProvider();
        else if (request.provider == ipc::Provider::Java)
            provider = createNativeJavaCandidateProvider();
        // No silent fallback to UIA/MSAA or in-process Java discovery.
        if (!provider) return 70;
        ipc::Reply reply;
        reply.operation=request.operation;reply.provider=request.provider;
        reply.nonce=request.nonce;
        if(request.operation==ipc::Operation::Scan){
            reply.scan = candidates::normalize(provider->enumerate(
                request.target,request.limits,{}),request.limits);
        }else if(request.operation==ipc::Operation::Verify){
            if (request.provider != ipc::Provider::Uia) return 71;
            reply.verification=provider->verify(request.target,request.candidate,{});
        }else return 71;
        std::vector<std::uint8_t> payload,framed;
        if(!ipc::encode(reply,payload)||!ipc::frame(payload,framed))return 72;
        std::cout.write(reinterpret_cast<const char*>(framed.data()),
                        static_cast<std::streamsize>(framed.size()));
        std::cout.flush();
        return std::cout.good()?0:73;
    }catch(...){return 74;}
#endif
}
} // namespace clicknova::gui::qml
