module carven:test.internal.backend.generation.tuples;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.decl;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target.type;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct TupleGenerationQuery final {
    const TargetUnit& unit;
    bool void_abi;
    bool tuple_positions;
    std::size_t aliases;

    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool;
    auto visit_type(TargetTypeID id) noexcept -> bool;
    auto visit_variable(const TargetVariableStmt& variable) noexcept -> bool;
};

struct StaticProductQuery final {
    const TargetUnit& unit;
    bool retained_product;

    auto visit_type(TargetTypeID id) noexcept -> bool {
        const auto* type = std::get_if<TargetIntrinsicType>(&unit.type(id).value);
        retained_product |= type && type->symbol == TargetSymbol::RuntimeTuple;
        return true;
    }
};

auto TupleGenerationQuery::enter_declaration(const TargetDecl& declaration) noexcept -> bool {
    const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
    if (function == nullptr || function->name.components().back().spelling() != "nothing") {
        return true;
    }
    const auto* result = std::get_if<TargetIntrinsicType>(&unit.type(function->result).value);
    if (!expect_equal(function->parameters.size(), 1uz)) {
        return false;
    }
    const auto* parameter =
        std::get_if<TargetIntrinsicType>(&unit.type(function->parameters.front().type).value);
    void_abi |= result != nullptr
        && result->symbol == TargetSymbol::Void
        && parameter != nullptr
        && parameter->symbol == TargetSymbol::RuntimeVoid;
    return true;
}

auto TupleGenerationQuery::visit_type(TargetTypeID id) noexcept -> bool {
    const auto* tuple = std::get_if<TargetIntrinsicType>(&unit.type(id).value);
    if (tuple == nullptr
        || tuple->symbol != TargetSymbol::RuntimeTuple
        || tuple->type_argument_ids.size() != 3uz) {
        return true;
    }
    const auto* middle =
        std::get_if<TargetIntrinsicType>(&unit.type(tuple->type_argument_ids[1]).value);
    tuple_positions |= middle != nullptr && middle->symbol == TargetSymbol::RuntimeVoid;
    return true;
}

auto TupleGenerationQuery::visit_variable(const TargetVariableStmt& variable) noexcept -> bool {
    aliases += variable.binding == TargetVariableBinding::MutableReference;
    return true;
}

const TestSuite suite([] static noexcept {
    "Generation: static decomposition delivers scalar facts without product storage"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "export(cpp) fn answer() -> i32 { "
                    "const (left, _, (right,)) = (20, (), (22,)); return left + right; }"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("static_tuple_values")}
            );
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = StaticProductQuery {.unit = unit, .retained_product = false};
                require(traverse_target_unit(unit.sections(), query));
                expect(!query.retained_product);
            }
        };
    "Generation: void values preserve tuple positions and native return ABI"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "export(cpp) fn nothing(value: void) -> void => value; "
                    "export(cpp) fn product() -> (i32, void, i32) => (3, nothing(()), 4); "
                    "fn probe() -> i32 { let (first, empty, last) = product(); "
                    "nothing(empty); return first + last; }"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("tuple_values")}
            );
            auto void_abi = false;
            auto tuple_positions = false;
            auto aliases = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = TupleGenerationQuery {
                    .unit = unit,
                    .void_abi = false,
                    .tuple_positions = false,
                    .aliases = 0uz
                };
                require(traverse_target_unit(unit.sections(), query));
                void_abi |= query.void_abi;
                tuple_positions |= query.tuple_positions;
                aliases += query.aliases;
            }
            expect(void_abi);
            expect(tuple_positions);
            expect(aliases >= 2uz);
        };
});

} // namespace
