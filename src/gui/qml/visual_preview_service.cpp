// SPDX-License-Identifier: MIT
#include "visual_preview_service.hpp"
#include "visual_isolated_transport.hpp"
#include <QMetaObject>
#include <QRandomGenerator>
#include <utility>

namespace clicknova::gui::qml {
using namespace clicknova::visual::worker;
VisualPreviewService::VisualPreviewService(QObject* parent):QObject(parent){
    armTimer_.setSingleShot(true);
    connect(&armTimer_,&QTimer::timeout,this,&VisualPreviewService::launch);
}
VisualPreviewService::~VisualPreviewService(){
    armTimer_.stop();
    if(thread_.joinable()){thread_.request_stop();thread_.join();}
}
bool VisualPreviewService::start(int row,std::uint64_t hwnd,std::uint32_t pid,
                                 Provider provider,std::string query){
    if(busy_||row<0)return false;
    Request pending{provider,QRandomGenerator::global()->generate64()|std::uint64_t{1},
                    hwnd,pid,std::move(query)};
    if(!valid(pending))return false;
    // Reap only a previously completed thread. No overlapping processes.
    if(thread_.joinable())thread_.join();
    request_=std::move(pending);
    row_=row;busy_=true;cancelRequested_=false;
    state_=QStringLiteral("arming");++generation_;
    armTimer_.start(3000); // leave time for the user to focus the target
    emit changed();
    return true;
}
void VisualPreviewService::launch(){
    if(!busy_||cancelRequested_)return;
    state_=QStringLiteral("searching");emit changed();
    const auto request=request_;
    const auto row=row_;
    const auto generation=generation_;
    thread_=std::jthread([this,request,row,generation](std::stop_token stop){
        visual_transport::Result result;
        try{result=visual_transport::inspect(request,stop);}
        catch(...){result={visual_transport::Outcome::Unavailable,{}};}
        const bool cancelled=stop.stop_requested();
        const int outcome=static_cast<int>(result.outcome);
        const int status=static_cast<int>(result.reply.status);
        QMetaObject::invokeMethod(this,[this,generation,row,cancelled,outcome,status]{
            finish(generation,row,cancelled,outcome,status);
        },Qt::QueuedConnection);
    });
}
void VisualPreviewService::cancel(){
    if(!busy_)return;
    cancelRequested_=true;
    if(armTimer_.isActive()){
        armTimer_.stop();busy_=false;
        request_.query.clear();
        state_=QStringLiteral("cancelled");emit changed();
        return;
    }
    if(thread_.joinable())thread_.request_stop();
    state_=QStringLiteral("cancelling");emit changed();
}
void VisualPreviewService::finish(std::uint64_t generation,int row,bool cancelled,
                                   int outcome,int resultStatus){
    if(generation!=generation_||row!=row_||!busy_)return;
    busy_=false;
    using visual_transport::Outcome;
    const auto o=static_cast<Outcome>(outcome);
    const auto status=static_cast<Status>(resultStatus);
    if(cancelled||cancelRequested_||o==Outcome::Cancelled)
        state_=QStringLiteral("cancelled");
    else if(o==Outcome::TimedOut)state_=QStringLiteral("timeout");
    else if(o==Outcome::Unavailable)state_=QStringLiteral("worker-unavailable");
    else if(o==Outcome::Malformed)state_=QStringLiteral("invalid-worker-reply");
    else if(o!=Outcome::Success)state_=QStringLiteral("failed");
    else switch(status){
        case Status::Found:state_=QStringLiteral("found");break;
        case Status::NotFound:state_=QStringLiteral("not-found");break;
        case Status::Unavailable:state_=QStringLiteral("provider-unavailable");break;
        case Status::UnsafeTarget:state_=QStringLiteral("unsafe-target");break;
        default:state_=QStringLiteral("failed");break;
    }
    // Do not cache the query or OCR output after request completion.
    request_.query.clear();
    emit changed();
}
QVariantMap VisualPreviewService::view(int selectedRow) const{
    QVariantMap result;
    const bool current=selectedRow>=0&&selectedRow==row_;
    result.insert(QStringLiteral("busy"),busy_);
    result.insert(QStringLiteral("statusKey"),current?state_:QStringLiteral("idle"));
    result.insert(QStringLiteral("providerKey"),current&&request_.provider==Provider::Image?
        QStringLiteral("image"):QStringLiteral("ocr"));
    result.insert(QStringLiteral("canCancel"),busy_);
    result.insert(QStringLiteral("waitingForFocus"),busy_&&armTimer_.isActive());
    // No OCR string, template name, bytes, full path, HWND or PID here.
    return result;
}
}
