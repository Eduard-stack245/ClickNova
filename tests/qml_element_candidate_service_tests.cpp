// SPDX-License-Identifier: MIT
#include "../src/gui/qml/element_candidate_service.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QThread>
#include <cstdlib>
#include <iostream>
#define CHECK(expr) do { if(!(expr)) {std::cerr << "failed line " << __LINE__ << ": " #expr "\n";std::abort();}} while(false)
using namespace clicknova::accessibility::candidates;
using clicknova::gui::qml::ElementCandidateService;
struct Fake final : IProvider {
    int count{};
    bool waitForCancel{};
    VerifyStatus verifyOutcome{VerifyStatus::Verified};
    int verificationCount{};
    VerifyResult verify(Target, const Candidate& candidate, std::stop_token stop) override {
        ++verificationCount;
        if (stop.stop_requested()) return {VerifyStatus::Cancelled, {}};
        return {verifyOutcome, verifyOutcome == VerifyStatus::Verified ? candidate : Candidate{}};
    }
    Result enumerate(Target,Limits,std::stop_token stop) override {
        ++count;
        if (waitForCancel) {
            for (int i=0;i<300 && !stop.stop_requested();++i) QThread::msleep(2);
            return Result{Status::Cancelled, {}, 0, false};
        }
        return Result{Status::Complete,{{50000,true,"run-unique","Button"},{50004,false}},2,false};
    }
};
static void pump(QCoreApplication& app,ElementCandidateService& svc) {
    QElapsedTimer clock;clock.start();
    while(svc.busy() && clock.elapsed()<4000) {
        app.processEvents();QThread::msleep(1);
    }
    app.processEvents();
    CHECK(!svc.busy());
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    ElementCandidateService service;
    auto provider=std::make_shared<Fake>();
    Context ctx;ctx.expectedRow=ctx.selectedRow=0;ctx.actionCount=1;
    ctx.workflowEditable=ctx.actionEditable=ctx.supportsElementSearch=true;
    ctx.provider=0;ctx.boundWindow=ctx.targetVerified=true;
    Target target{12345,42};
    Context conflicting = ctx;
    conflicting.alreadyScanning = true;
    CHECK(!service.start(conflicting,target,provider));
    CHECK(service.view(0,false).value("reasonKey") == QStringLiteral("busy"));
    CHECK(provider->count == 0);
    CHECK(service.start(ctx,target,provider));
    CHECK(!service.start(ctx,target,provider)); // non-reentrant while worker active
    pump(app,service);
    const auto result=service.view(0,false);
    CHECK(result.value("stateKey")==QStringLiteral("complete"));
    CHECK(result.value("items").toList().size()==2);
    CHECK(result.value("canBind").toBool());
    CHECK(result.value("items").toList().at(0).toMap().value("bindable").toBool());
    CHECK(!result.value("items").toList().at(1).toMap().value("bindable").toBool());
    CHECK(!result.value("verified").toBool());
    CHECK(service.view(1,false).value("items").toList().isEmpty());
    const auto json=QJsonDocument::fromVariant(result).toJson();
    CHECK(!json.contains("AutomationId") && !json.contains("windowTitle") &&
          !json.contains("nativeHandle") && !json.contains("selector") &&
          !json.contains("run-unique") && !json.contains("Button"));
    bool nativeVerified = false;
    QObject::connect(&service, &ElementCandidateService::verified, &service,
        [&](int row, const Candidate& match, Target live) {
            CHECK(row == 0 && live.processId == 42 && match.automationId == "run-unique");
            nativeVerified = true;
            service.bindingCommitted(true);
        });
    CHECK(!service.requestBind(0,0));
    CHECK(!service.requestBind(1,1));
    CHECK(!service.requestBind(0,2));
    CHECK(service.requestBind(0,1));
    CHECK(!service.requestBind(0,1));
    pump(app,service);
    CHECK(nativeVerified && provider->verificationCount == 1);
    CHECK(service.view(0,false).value("bindStateKey") == QStringLiteral("saved"));
    provider->verifyOutcome = VerifyStatus::Ambiguous;
    CHECK(service.requestBind(0,1));
    pump(app,service);
    CHECK(service.view(0,false).value("bindStateKey") == QStringLiteral("ambiguous"));
    ctx.selectedRow=99;
    CHECK(!service.start(ctx,target,provider));
    ctx.selectedRow=0;
    provider->waitForCancel=true;
    CHECK(service.start(ctx,target,provider));
    service.cancel();
    pump(app,service);
    CHECK(service.view(0,false).value("stateKey")==QStringLiteral("cancelled"));
    CHECK(service.view(0,false).value("items").toList().isEmpty());
    CHECK(provider->count==2);
    std::cout << "PASS async Qt candidate service, busy/abort/privacy/fail-closed\n";
}
