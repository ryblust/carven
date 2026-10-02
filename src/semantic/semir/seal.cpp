module carven:semantic.semir.seal.impl;

import :semantic.semir.program;
import :semantic.semir.traversal;
import :support.invariant;
import std;

auto SemIRBody::publish() noexcept -> void {
    if (data.residual) {
        data.region = std::move(*data.residual);
        data.residual.reset();
    }
    // Instance provenance belongs to the callable's program-level record.
    // Local identities remain owned by the copied tables.
    data.specialized.reset();
}

auto DeclarationStore::publish_bodies(std::span<const std::optional<BodyID>> bodies) noexcept
    -> void {
    auto rows = MutableProgramTable<CallableDeclaration, CallableID>(owner());
    body_callables.clear();
    for (auto& declaration : std::move(callable_rows).release()) {
        declaration.implementation.visit([&](auto& implementation) noexcept {
            using Implementation = std::remove_cvref_t<decltype(implementation)>;
            if constexpr (std::same_as<Implementation, FunctionBodyImplementation>) {
                implementation.body = bodies[implementation.body->index()];
            } else if constexpr (std::same_as<Implementation, ClosureBodyImplementation>) {
                const auto body = bodies[implementation.body.index()];
                if (!body) {
                    invariant_violation("closure has no executable body at publication");
                }
                implementation.body = *body;
            }
        });
        const auto body = callable_body_id(declaration);
        const auto callable = rows.add(declaration);
        if (body) {
            body_callables.emplace(*body, callable);
        }
    }
    callable_rows.storage = std::move(rows).seal().release();
}

auto SemIRProgram::publish_bodies() noexcept -> void {
    auto mapping = std::vector<std::optional<BodyID>>(body_store.rows.size());
    for (auto& owned : body_store.rows.storage) {
        auto& body = *owned;
        const auto original = body.id();
        if (!executed_bodies[original.index()]) {
            owned.reset();
            continue;
        }
        body.publish();
        visit_semantic_nodes(body.region(), [](const auto& node) static noexcept {
            using Node = std::remove_cvref_t<decltype(node)>;
            if constexpr (std::same_as<Node, SemanticStatement>) {
                if (std::holds_alternative<SemStaticBinding>(node.value)
                    || std::holds_alternative<SemConstBlock>(node.value)) {
                    invariant_violation("published executable body retains a static root");
                }
                if (const auto* loop = std::get_if<SemRangeLoop>(&node.value);
                    loop && loop->is_static) {
                    invariant_violation("published executable body retains static iteration");
                }
            } else if constexpr (std::same_as<Node, SemanticExpression>) {
                if (const auto* branch = std::get_if<SemIf>(&node.value);
                    branch && branch->is_static) {
                    invariant_violation("published executable body retains static selection");
                }
            }
        });
        mapping[original.index()] = original;
    }
    declaration_store.publish_bodies(mapping);
    for (const auto entry : declarations().callables()) {
        if (const auto body = callable_body_id(entry.value)) {
            if (!body_store.contains(*body)
                || callable_signatures().signature(entry.value.signature).has_static_parameters()) {
                invariant_violation("published callable does not have an executable contract");
            }
        }
    }
    auto tests = MutableProgramTable<TestDeclaration, TestID>(identity());
    for (auto& test : std::move(test_store.rows).release()) {
        test.body = mapping[test.body->index()];
        if (test.is_const == test.body.has_value()) {
            invariant_violation("test publication disagrees with its execution stage");
        }
        tests.add(test);
    }
    test_store.rows.storage = std::move(tests).seal().release();
}
