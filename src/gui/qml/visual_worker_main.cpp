// SPDX-License-Identifier: MIT
#include "visual_worker_main.hpp"
#include "visual_worker_capture_win32.hpp"
#include "clicknova/visual_worker_protocol.hpp"
#include <cstdio>
#include <iostream>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace clicknova::gui::qml {
int runVisualWorker() noexcept {
#ifndef _WIN32
    return 77;
#else
    using namespace clicknova::visual::worker;
    try {
        if(_setmode(_fileno(stdin),_O_BINARY)<0||_setmode(_fileno(stdout),_O_BINARY)<0)
            return 61;
        std::uint8_t length[4]{};
        if(!std::cin.read(reinterpret_cast<char*>(length),4))return 62;
        const auto n=std::uint32_t(length[0])|(std::uint32_t(length[1])<<8)|
            (std::uint32_t(length[2])<<16)|(std::uint32_t(length[3])<<24);
        if(n==0||n>maxFrame)return 63;
        std::vector<std::uint8_t> data(n);
        if(!std::cin.read(reinterpret_cast<char*>(data.data()),n))return 64;
        // Exact 1-request lifetime, and no argument-controlled file paths.
        if(std::cin.peek()!=std::char_traits<char>::eof())return 65;
        Request request;
        if(!decode(data,request))return 66;
        const auto reply=inspectVisualTarget(request);
        std::vector<std::uint8_t> body,packet;
        if(!encode(reply,body)||!frame(body,packet))return 67;
        std::cout.write(reinterpret_cast<const char*>(packet.data()),
                        static_cast<std::streamsize>(packet.size()));
        std::cout.flush();
        return std::cout.good()?0:68;
    }catch(...){return 69;}
#endif
}
} // namespace clicknova::gui::qml
