// SPDX-License-Identifier: MIT
#include "isolated_uia_provider.hpp"
#include "uia_isolated_transport.hpp"
#include <QCoreApplication>
#include <QRandomGenerator>

namespace clicknova::gui::qml {
#ifdef _WIN32
namespace {
using namespace clicknova::accessibility;
class IsolatedUiaProvider final : public candidates::IProvider {
    ipc::Provider provider_;
    ipc::Request request(ipc::Operation operation,candidates::Target target,
                                candidates::Limits limits) {
        ipc::Request r;
        r.operation=operation;
        r.provider=provider_;
        r.nonce=QRandomGenerator::global()->generate64() | std::uint64_t{1};
        r.target=target;r.limits=limits;return r;
    }
    static uia_transport::Transaction call(const ipc::Request& r,int timeout,
                                           std::stop_token token) {
        return uia_transport::transact(QCoreApplication::applicationFilePath(),
            {QStringLiteral("--uia-worker")},r,timeout,token);
    }
public:
    explicit IsolatedUiaProvider(ipc::Provider provider):provider_(provider){}
    candidates::Result enumerate(candidates::Target target,candidates::Limits limits,
                                 std::stop_token cancel) override {
        if(cancel.stop_requested())return {candidates::Status::Cancelled,{},0,false};
        const auto r=request(ipc::Operation::Scan,target,limits);
        const auto answer=call(r,4000,cancel);
        if(answer.outcome==uia_transport::Outcome::Cancelled || cancel.stop_requested())
            return {candidates::Status::Cancelled,{},0,false};
        if(answer.outcome!=uia_transport::Outcome::Success)
            return {candidates::Status::Failed,{},0,false};
        return candidates::normalize(answer.reply.scan,limits);
    }
    candidates::VerifyResult verify(candidates::Target target,const candidates::Candidate& candidate,
                                     std::stop_token cancel) override {
        if(cancel.stop_requested())return {candidates::VerifyStatus::Cancelled,{}};
        if(provider_ != ipc::Provider::Uia) return {candidates::VerifyStatus::Unbindable,{}};
        if(!candidates::eligible(candidate))return {candidates::VerifyStatus::Unbindable,{}};
        const candidates::Limits verifyLimits{512,32,10,3000};
        auto r=request(ipc::Operation::Verify,target,verifyLimits);
        r.candidate=candidate;
        const auto answer=call(r,6500,cancel);
        if(answer.outcome==uia_transport::Outcome::Cancelled || cancel.stop_requested())
            return {candidates::VerifyStatus::Cancelled,{}};
        if(answer.outcome!=uia_transport::Outcome::Success)
            return {candidates::VerifyStatus::Failed,{}};
        const auto& check=answer.reply.verification;
        if(check.status==candidates::VerifyStatus::Verified &&
           (!candidates::eligible(check.match)||!candidates::sameIdentity(candidate,check.match)))
            return {candidates::VerifyStatus::Failed,{}};
        return check;
    }
};
}
std::shared_ptr<accessibility::candidates::IProvider> createIsolatedUiaCandidateProvider() {
    return std::make_shared<IsolatedUiaProvider>(ipc::Provider::Uia);
}
std::shared_ptr<accessibility::candidates::IProvider>
createIsolatedAccessibilityCandidateProvider(int provider) {
    if (provider < 0 || provider > 2) return {};
    return std::make_shared<IsolatedUiaProvider>(static_cast<ipc::Provider>(provider));
}
#else
std::shared_ptr<accessibility::candidates::IProvider> createIsolatedUiaCandidateProvider() {
    return {};
}
std::shared_ptr<accessibility::candidates::IProvider>
createIsolatedAccessibilityCandidateProvider(int) {return {};}
#endif
} // namespace clicknova::gui::qml
