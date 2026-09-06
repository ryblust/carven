module carven:semantic.semir.structured.impl;

import :semantic.semir.structured;
import :semantic.semir.traversal;
import :support.visit;
import std;

SemIRBody::SemIRBody(SemIRBodyData data) noexcept
    : data(std::move(data)) {
    visit_semantic_nodes(
        this->data.region,
        Overloaded {
            [](const SemanticRegion& region) static noexcept {
                static_cast<void>(region.failures.resolved());
            },
            [](const SemanticExpression& expression) static noexcept {
                static_cast<void>(expression.type.resolved());
                static_cast<void>(expression.failures.resolved());
                if (const auto* call = std::get_if<SemCall>(&expression.value)) {
                    static_cast<void>(call->callee_failures.resolved());
                }
                if (const auto* attempt = std::get_if<SemTry>(&expression.value)) {
                    static_cast<void>(attempt->protected_failures.resolved());
                    static_cast<void>(attempt->residual_failures.resolved());
                    for (const auto& arm : attempt->arms) {
                        static_cast<void>(arm.accepted_failures.resolved());
                        for (const auto& alternative : arm.alternatives) {
                            if (const auto* typed =
                                    std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                                static_cast<void>(typed->type.resolved());
                            }
                        }
                    }
                }
            },
        }
    );
}
