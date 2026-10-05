module carven:test.internal.backend.generation.addressof;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.expr;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Generation: addressof preserves a place through SemIR and C++ lowering"_test =
        [] static noexcept {
            auto semantic = analyze_test_program(
                "fn writable(&value: i32) -> ptr<&i32> => addressof(&value); "
                "fn readable(value: i32) -> i32 { "
                "  let address = addressof(value); "
                "  if address == nullptr { return 0; } "
                "  return *address; "
                "}"
            );
            auto address_operations = 0uz;
            for (const auto entry : semantic.bodies().entries()) {
                visit_semantic_nodes(
                    entry.value.region(),
                    [&](const SemanticExpression& expression) noexcept {
                        if (const auto* address = std::get_if<SemAddressOf>(&expression.value)) {
                            expect(address->source->category == SemanticValueCategory::Place);
                            expect(std::holds_alternative<SemBinding>(address->source->value));
                            ++address_operations;
                        }
                    }
                );
            }
            expect(address_operations == 2uz);

            const auto compilation = PlannedCompilation::build(
                std::move(semantic),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("addressof")}
            );

            struct Query final {
                std::size_t address_calls = 0uz;

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    const auto* call = std::get_if<TargetCallExpr>(&expression.value);
                    if (call == nullptr) {
                        return true;
                    }
                    const auto* callee = std::get_if<TargetIntrinsicNameExpr>(
                        &template_primary_expression(*call->callee).value
                    );
                    if (callee != nullptr && callee->symbol == TargetSymbol::StdAddressof) {
                        expect(call->arguments.size() == 1uz);
                        ++address_calls;
                    }
                    return true;
                }
            };

            auto query = Query {};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                expect(traverse_target_unit(unit.sections(), query));
            }
            expect(query.address_calls == 2uz);
        };

    "Generation: slice element addresses lower through the borrowed index place"_test =
        [] static noexcept {
            auto semantic = analyze_test_program(R"(
        fn element(values: [i32; 2]) -> ptr<i32> {
            let view = values.as_slice();
            return addressof(view[1]);
        }
    )");
            auto indexed_addresses = 0uz;
            for (const auto entry : semantic.bodies().entries()) {
                visit_semantic_nodes(
                    entry.value.region(),
                    [&](const SemanticExpression& expression) noexcept {
                        const auto* address = std::get_if<SemAddressOf>(&expression.value);
                        if (address == nullptr) {
                            return;
                        }
                        const auto* index = std::get_if<SemIndex>(&address->source->value);
                        if (!expect(index != nullptr)) {
                            return;
                        }
                        expect(address->source->category == SemanticValueCategory::Place);
                        expect(
                            std::holds_alternative<SliceTypeValue>(
                                semantic.types().type(index->source->type.resolved()).value
                            )
                        );
                        ++indexed_addresses;
                    }
                );
            }
            expect(indexed_addresses == 1uz);

            const auto compilation = PlannedCompilation::build(
                std::move(semantic),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("slice_element_address")}
            );

            struct Query final {
                std::size_t addresses = 0uz;
                std::size_t indices = 0uz;

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    if (std::holds_alternative<TargetIndexExpr>(expression.value)) {
                        ++indices;
                    }
                    const auto* call = std::get_if<TargetCallExpr>(&expression.value);
                    const auto* callee = call
                        ? std::get_if<TargetIntrinsicNameExpr>(
                              &template_primary_expression(*call->callee).value
                          )
                        : nullptr;
                    if (callee != nullptr && callee->symbol == TargetSymbol::StdAddressof) {
                        ++addresses;
                    }
                    if (callee != nullptr
                        && callee->symbol == TargetSymbol::RuntimeCheckedSliceIndex) {
                        ++indices;
                    }
                    return true;
                }
            };

            auto query = Query {};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                expect(traverse_target_unit(unit.sections(), query));
            }
            expect(query.addresses == 1uz);
            expect(query.indices >= 1uz);
        };
});

} // namespace
