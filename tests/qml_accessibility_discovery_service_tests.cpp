// SPDX-License-Identifier: MIT
#include "../src/gui/qml/accessibility_discovery_service.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTimer>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "Check failed at " << __LINE__ << ": " #expr << "\n"; std::abort(); } } while (false)

using clicknova::gui::qml::AccessibilityDiscoveryService;
using clicknova::gui::qml::IAccessibilityDiscoveryBackend;
using namespace clicknova::accessibility::discovery;

struct Mock final : IAccessibilityDiscoveryBackend {
    AccessibilityDiscoveryService* service{};
    Request context{};
    bool enabled{true};
    bool delivered{true};
    bool throwError{false};
    bool reenter{false};
    int invokes{0};
    int nestedBlocked{0};
    bool available(Operation) const override { return enabled; }
    bool invoke(Operation, int expectedRow) override {
        CHECK(expectedRow == context.expectedRow);
        ++invokes;
        if (reenter) {
            // Simulate a nested event loop in a modal native picker.
            QCoreApplication::processEvents();
            if (!service->request(context, *this)) ++nestedBlocked;
        }
        if (throwError) throw std::runtime_error("PRIVATE_WINDOW_SECRET");
        return delivered;
    }
};
int main(int argc, char* argv[]) {
    QCoreApplication app(argc,argv);
    AccessibilityDiscoveryService svc;
    Mock backend;
    backend.service = &svc;
    backend.context.operation = Operation::Find;
    backend.context.expectedRow = backend.context.selectedRow = 1;
    backend.context.actionCount = 2;
    backend.context.actionEditable = true;
    backend.context.workflowEditable = true;
    backend.context.facts.step = clicknova::accessibility::StepKind::LeftClick;
    const auto valid = backend.context;
    CHECK(svc.canRequest(valid,backend));
    CHECK(svc.view(1,false).value("stateKey").toString() == QStringLiteral("idle"));
    backend.reenter=true;
    CHECK(svc.request(valid,backend));
    CHECK(backend.invokes == 1 && backend.nestedBlocked == 1);
    CHECK(svc.view(1,false).value("stateKey").toString() == QStringLiteral("native-tool-closed"));
    CHECK(!svc.view(1,false).value("verified").toBool());
    CHECK(svc.view(0,false).value("stateKey").toString() == QStringLiteral("idle"));
    CHECK(!svc.busy());
    backend.reenter=false;
    backend.enabled=false;
    CHECK(!svc.canRequest(valid,backend));
    CHECK(!svc.request(valid,backend));
    CHECK(backend.invokes == 1);
    CHECK(svc.view(1,true).value("reasonKey").toString() == QStringLiteral("native-unavailable"));
    backend.enabled=true;
    backend.delivered=false;
    CHECK(!svc.request(valid,backend));
    CHECK(backend.invokes == 2);
    CHECK(svc.view(1,false).value("stateKey").toString() == QStringLiteral("not-delivered"));
    backend.throwError=true;
    CHECK(!svc.request(valid,backend));
    const auto serialized = QJsonDocument::fromVariant(svc.view(1,false)).toJson();
    CHECK(!serialized.contains("PRIVATE_WINDOW_SECRET"));
    backend.throwError=false;
    backend.delivered=true;
    Request outdated=valid;
    outdated.selectedRow = 0;
    CHECK(!svc.request(outdated,backend));
    CHECK(backend.invokes==3);
    CHECK(svc.view(1,false).value("reasonKey").toString() == QStringLiteral("stale-selection"));
    Request locked=valid;
    locked.workflowEditable=false;
    CHECK(!svc.request(locked,backend));
    CHECK(backend.invokes==3);
    Request clearing=valid;
    clearing.operation = Operation::Clear;
    CHECK(!svc.request(clearing,backend));
    CHECK(backend.invokes==3);
    clearing.facts.hasAutomationId=true;
    CHECK(svc.request(clearing,backend));
    CHECK(backend.invokes==4);
    std::cout << "PASS Qt discovery service reentry, stale selection, fail-closed, privacy\n";
}
