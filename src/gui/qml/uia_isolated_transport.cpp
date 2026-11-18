// SPDX-License-Identifier: MIT
#include "uia_isolated_transport.hpp"
#include <QByteArray>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <algorithm>
#include <vector>

namespace clicknova::gui::qml::uia_transport {
Transaction transact(const QString& executable, const QStringList& arguments,
                     const accessibility::ipc::Request& request, int timeoutMs,
                     std::stop_token cancel) {
    using namespace clicknova::accessibility::ipc;
    if (cancel.stop_requested()) return {Outcome::Cancelled, {}};
    // No arbitrary command execution and no environment-dependent PATH search.
    if (timeoutMs < 100 || timeoutMs > 10000 || !QFileInfo(executable).isAbsolute() ||
        !QFileInfo(executable).isFile() || QFileInfo(executable).isSymLink())
        return {Outcome::Unavailable, {}};
    std::vector<std::uint8_t> payload, requestFrame;
    if (!encode(request, payload) || !frame(payload, requestFrame))
        return {Outcome::Malformed, {}};
    const QByteArray input(reinterpret_cast<const char*>(requestFrame.data()),
                           static_cast<qsizetype>(requestFrame.size()));
    QProcess proc;
    proc.setProgram(executable);
    proc.setArguments(arguments);
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.setStandardErrorFile(QProcess::nullDevice()); // Never capture private COM diagnostics.
    QElapsedTimer wall;
    wall.start();
    proc.start(QIODevice::ReadWrite);
    auto remaining = [&] {return std::max(0,timeoutMs-static_cast<int>(wall.elapsed()));};
    auto abort = [&] {
        if(proc.state()!=QProcess::NotRunning) {
            proc.kill();
            (void)proc.waitForFinished(750); // bounded reap on Windows
        }
    };
    if(!proc.waitForStarted(std::min(remaining(),600))) {
        abort();return {Outcome::Unavailable, {}};
    }
    if(cancel.stop_requested()) {abort();return {Outcome::Cancelled,{}};}
    if(proc.write(input)!=input.size()) {abort();return {Outcome::Unavailable,{}};}
    while(proc.bytesToWrite()>0) {
        if(cancel.stop_requested()){abort();return {Outcome::Cancelled,{}};}
        if(!remaining()){abort();return {Outcome::TimedOut,{}};}
        if(!proc.waitForBytesWritten(std::min(remaining(),50)) && proc.state()==QProcess::NotRunning) {
            abort();return {Outcome::Unavailable,{}};
        }
    }
    proc.closeWriteChannel();
    QByteArray output;
    while(proc.state()!=QProcess::NotRunning) {
        if(cancel.stop_requested()) {abort();return {Outcome::Cancelled,{}};}
        if(!remaining()) {abort();return {Outcome::TimedOut,{}};}
        (void)proc.waitForFinished(std::min(remaining(),40));
        output.append(proc.readAllStandardOutput());
        if(output.size()>static_cast<qsizetype>(maxFrame+4)) {
            abort();return {Outcome::Malformed,{}};
        }
    }
    output.append(proc.readAllStandardOutput());
    if(cancel.stop_requested())return {Outcome::Cancelled,{}};
    if(proc.exitStatus()!=QProcess::NormalExit || proc.exitCode()!=0 ||
       output.size()>static_cast<qsizetype>(maxFrame+4))return {Outcome::Unavailable,{}};
    std::vector<std::uint8_t> returned;
    const auto* bytes=reinterpret_cast<const std::uint8_t*>(output.constData());
    if(!unframe({bytes,static_cast<std::size_t>(output.size())},returned))
        return {Outcome::Malformed,{}};
    Reply reply;
    if(!decode(returned,reply) || !matchesRequest(request,reply))
        return {Outcome::Malformed,{}};
    return {Outcome::Success,std::move(reply)};
}
} // namespace clicknova::gui::qml::uia_transport
