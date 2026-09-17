module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.constant_slices;

import :artifacts;
import :backend.emission.emit;
import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target.type;
import :backend.target;
import :frontend.program.parse;
import :semantic.analyze;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct StaticSliceFacts final {
    const TargetUnit& unit;
    std::size_t declarations = 0;
    std::size_t slices = 0;
    std::size_t empty_arrays = 0;
    std::vector<std::string> referenced_names;

    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool {
        const auto* variable = std::get_if<TargetVariableDecl>(&declaration);
        if (variable == nullptr) {
            return true;
        }
        ++declarations;
        CHECK(variable->inline_specifier);
        CHECK(variable->constexpr_specifier);
        const auto* declaration_type =
            std::get_if<TargetIntrinsicType>(&unit.type(variable->type).value);
        REQUIRE(declaration_type != nullptr);
        CHECK(declaration_type->symbol == TargetSymbol::Auto);
        const auto* array = std::get_if<TargetArrayExpr>(&variable->initializer.value);
        REQUIRE(array != nullptr);
        const auto& element = unit.type(array->element_type_id).value;
        CHECK(
            (std::holds_alternative<TargetIntrinsicType>(element)
             || std::holds_alternative<TargetArrayType>(element))
        );
        const auto* literal = std::get_if<TargetLiteralExpr>(&array->extent->value);
        REQUIRE(literal != nullptr);
        const auto* extent = std::get_if<TargetIntegerLiteral>(&literal->value);
        REQUIRE(extent != nullptr);
        CHECK(extent->magnitude == array->elements.size());
        empty_arrays += extent->magnitude == 0;
        return true;
    }

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool {
        const auto* call = std::get_if<TargetCallExpr>(&expression.value);
        if (call == nullptr) {
            return true;
        }
        const auto* callee = std::get_if<TargetIntrinsicNameExpr>(&call->callee->value);
        if (callee == nullptr || callee->symbol != TargetSymbol::RuntimeAsSlice) {
            return true;
        }
        const auto* argument = call->arguments.size() == 1
            ? std::get_if<TargetNameExpr>(&call->arguments[0].value)
            : nullptr;
        // Count only views of named static objects; runtime borrows can use local places.
        if (argument == nullptr || !argument->name.is_globally_qualified()) {
            return true; // The ordinary runtime borrow uses its parameter.
        }
        ++slices;
        auto name = std::string();
        for (const auto& component : argument->name.components()) {
            name += "::";
            name += component.spelling();
        }
        referenced_names.push_back(std::move(name));
        return true;
    }
};

} // namespace

TEST_CASE("Generation: frozen slices reference deduplicated static array declarations") {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(R"(
            const numbers: [i32] = make();
            const empty: [i32] = [];
            const nested: [[i32; 2]] = [[1, 2], [3, 4]];
            const text: [str] = ["我\0", "😀"];
            const fn make() -> [i32; 2] => [1, 2];
            fn first() -> [i32] => numbers;
            fn second() -> [i32] => numbers;
            fn empty_view() -> [i32] => empty;
            fn nested_view() -> [[i32; 2]] => nested;
            fn text_view() -> [str] => text;
            fn ordinary(value: [i32; 2]) -> [i32] => value;
        )"),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("constant_slices")}
    );
    auto declarations = 0uz;
    auto slices = 0uz;
    auto empty_arrays = 0uz;
    for (const auto artifact : compilation.target().artifacts()) {
        auto unit = lower_artifact(compilation, artifact.id);
        auto facts = StaticSliceFacts {.unit = unit, .referenced_names = {}};
        REQUIRE(traverse_target_unit(unit.sections(), facts));
        declarations += facts.declarations;
        slices += facts.slices;
        empty_arrays += facts.empty_arrays;
        if (facts.slices != 0) {
            const auto unique = std::flat_set<std::string>(
                facts.referenced_names.begin(),
                facts.referenced_names.end()
            );
            CHECK(unique.size() == facts.declarations);
            CHECK(facts.slices > facts.declarations);
            const auto generated = emit(
                std::move(unit),
                "constant_slices.cpp",
                GeneratedArtifactRole::ModuleImplementation,
                SourceAttributedEmission {.generated_origin = "constant_slices.cpp"}
            );
            CHECK(generated.content.contains("inline constexpr auto "));
            CHECK(generated.content.contains("carven::runtime::as_slice("));
            CHECK(generated.content.contains("::carven::generated::"));
            CHECK(generated.content.contains("#include <array>"));
            CHECK(generated.content.contains("#include <string_view>"));
            CHECK(generated.content.contains("#include <carven/runtime/slice.hpp>"));
            CHECK(
                generated.content.find("inline constexpr") < generated.content.find("auto first(")
            );
        }
    }
    CHECK(declarations == 4uz);
    CHECK(slices == 5uz);
    CHECK(empty_arrays == 1uz);
}

TEST_CASE("Generation: static slice backing names are isolated across artifact owners") {
    auto sources = SourceManager();
    auto inputs = std::vector<SourceModuleInput>();
    const auto append = [&](std::string_view name, std::string source) noexcept {
        const auto id = sources.append_virtual(std::format("{}.cv", name), std::move(source));
        REQUIRE(id.has_value());
        const auto path = CanonicalModulePath::from_value(name);
        REQUIRE(path.has_value());
        inputs.push_back({.source_id = *id, .module_path = *path});
    };
    append(
        "provider",
        "export const numbers: [i32] = [1, 2]; fn provider_view() -> [i32] => numbers;"
    );
    append("consumer", "import provider using numbers; fn consumer_view() -> [i32] => numbers;");
    auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
    REQUIRE(parsed.has_value());
    auto analyzed = analyze(std::move(*parsed));
    REQUIRE(analyzed.has_value());
    const auto compilation = PlannedCompilation::build(
        std::move(analyzed->value),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("cross_artifact_slices")}
    );
    auto names = std::flat_set<std::string>();
    auto declarations = 0uz;
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        auto facts = StaticSliceFacts {.unit = unit, .referenced_names = {}};
        REQUIRE(traverse_target_unit(unit.sections(), facts));
        declarations += facts.declarations;
        for (const auto& name : facts.referenced_names) {
            CHECK(names.insert(name).second);
        }
    }
    CHECK(declarations == 2uz);
    CHECK(names.size() == 2uz);
}
