// SPDX-License-Identifier: MIT
#include "visual_isolated_transport.hpp"
#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <algorithm>
#include <vector>

namespace clicknova::gui::qml::visual_transport {
Result inspect(const clicknova::visual::worker::Request& request,
               std::stop_token cancel) {
    using namespace clicknova::visual::worker;
    if(cancel.stop_requested())return {Outcome::Cancelled,{}};
    if(!valid(request))return {Outcome::Malformed,{}};
#ifndef _WIN32
    return {Outcome::Unavailable,{}};
#else
    const QString own=QCoreApplication::applicationFilePath();
    const QFileInfo executable(own);
    if(!executable.isAbsolute()||!executable.isFile()||executable.isSymLink())
        return {Outcome::Unavailable,{}};
    std::vector<std::uint8_t> payload,packet;
    if(!encode(request,payload)||!frame(payload,packet))return {Outcome::Malformed,{}};
    QByteArray input(reinterpret_cast<const char*>(packet.data()),
                     static_cast<qsizetype>(packet.size()));
    const int timeoutMs=request.provider==Provider::Ocr?6500:4500;
    QProcess worker;
    worker.setProgram(own);
    worker.setArguments({QStringLiteral("--visual-worker")});
    worker.setProcessChannelMode(QProcess::SeparateChannels);
    worker.setStandardErrorFile(QProcess::nullDevice());
    QElapsedTimer elapsed;elapsed.start();
    const auto left=[&]{return std::max(0,timeoutMs-static_cast<int>(elapsed.elapsed()));};
    const auto abort=[&]{
        if(worker.state()!=QProcess::NotRunning){
            worker.kill();(void)worker.waitForFinished(300);
        }
    };
    worker.start(QIODevice::ReadWrite);
    if(!worker.waitForStarted(std::min(left(),700))){abort();return {Outcome::Unavailable,{}};}
    if(cancel.stop_requested()){abort();return {Outcome::Cancelled,{}};}
    if(worker.write(input)!=input.size()){abort();return {Outcome::Unavailable,{}};}
    while(worker.bytesToWrite()>0) {
        if(cancel.stop_requested()){abort();return {Outcome::Cancelled,{}};}
        if(!left()){abort();return {Outcome::TimedOut,{}};}
        (void)worker.waitForBytesWritten(std::min(left(),45));
        if(worker.state()==QProcess::NotRunning&&worker.bytesToWrite()>0){
            abort();return {Outcome::Unavailable,{}};
        }
    }
    worker.closeWriteChannel();
    QByteArray output;
    while(worker.state()!=QProcess::NotRunning){
        if(cancel.stop_requested()){abort();return {Outcome::Cancelled,{}};}
        if(!left()){abort();return {Outcome::TimedOut,{}};}
        (void)worker.waitForFinished(std::min(left(),45));
        output.append(worker.readAllStandardOutput());
        if(output.size()>static_cast<qsizetype>(maxFrame+4)){
            abort();return {Outcome::Malformed,{}};
        }
    }
    output.append(worker.readAllStandardOutput());
    if(cancel.stop_requested())return {Outcome::Cancelled,{}};
    if(worker.exitStatus()!=QProcess::NormalExit||worker.exitCode()!=0)
        return {Outcome::Unavailable,{}};
    if(output.size()>static_cast<qsizetype>(maxFrame+4))
        return {Outcome::Malformed,{}};
    std::vector<std::uint8_t> body;
    const auto* raw=reinterpret_cast<const std::uint8_t*>(output.constData());
    if(!unframe({raw,static_cast<std::size_t>(output.size())},body))
        return {Outcome::Malformed,{}};
    Reply reply;
    if(!decode(body,reply)||!matches(request,reply))
        return {Outcome::Malformed,{}};
    return {Outcome::Success,reply};
#endif
}
}
