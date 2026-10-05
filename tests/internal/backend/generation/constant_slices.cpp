module carven:test.internal.backend.generation.constant_slices;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.name;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target.type;
import :frontend.program.parse;
import :semantic.analyze;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct StaticSliceFacts final {
    const TargetUnit& unit;
    std::size_t declarations;
    std::size_t slices;
    std::size_t empty_arrays;
    std::vector<std::string> referenced_names;
    bool saw_function_definition;
    std::vector<std::string_view> source_names;

    auto enter_item(const TargetItem& item) noexcept -> bool {
        if (const auto* space = std::get_if<TargetNamespace>(&item.value); space && space->name) {
            const auto name = space->name->components().back().spelling();
            expect(!(std::ranges::contains(source_names, name)));
        }
        return true;
    }

    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool {
        const auto* variable = std::get_if<TargetVariableDecl>(&declaration);
        if (variable == nullptr) {
            if (const auto* function = std::get_if<TargetFunctionDecl>(&declaration)) {
                saw_function_definition |=
                    std::holds_alternative<TargetFreeFunctionDefinition>(function->form);
            }
            return true;
        }
        expect(!(saw_function_definition));
        ++declarations;
        expect(variable->inline_specifier);
        expect(variable->constexpr_specifier);
        const auto* declaration_type =
            std::get_if<TargetIntrinsicType>(&unit.type(variable->type).value);
        if (!expect(declaration_type != nullptr)) {
            return false;
        }
        expect_equal(declaration_type->symbol, TargetSymbol::Auto);
        const auto* array = std::get_if<TargetArrayExpr>(&variable->initializer.value);
        if (!expect(array != nullptr)) {
            return false;
        }
        const auto& element = unit.type(array->element_type_id).value;
        expect(
            (std::holds_alternative<TargetIntrinsicType>(element)
             || std::holds_alternative<TargetArrayType>(element))
        );
        const auto* literal = std::get_if<TargetLiteralExpr>(&array->extent->value);
        if (!expect(literal != nullptr)) {
            return false;
        }
        const auto* extent = std::get_if<TargetIntegerLiteral>(&literal->value);
        if (!expect(extent != nullptr)) {
            return false;
        }
        expect(extent->magnitude == array->elements.size());
        empty_arrays += extent->magnitude == 0;
        return true;
    }

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool {
        const auto* call = std::get_if<TargetCallExpr>(&expression.value);
        if (call == nullptr) {
            return true;
        }
        const auto* callee =
            std::get_if<TargetIntrinsicNameExpr>(&template_primary_expression(*call->callee).value);
        if (callee == nullptr || callee->symbol != TargetSymbol::RuntimeAsSlice) {
            return true;
        }
        const auto* argument = call->arguments.size() == 1uz
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

const TestSuite suite([] static noexcept {
    "Generation: frozen slices reference deduplicated static array declarations"_test =
        [] static noexcept {
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
                const auto unit = lower_artifact(compilation, artifact.id);
                auto facts = StaticSliceFacts {
                    .unit = unit,
                    .declarations = 0uz,
                    .slices = 0uz,
                    .empty_arrays = 0uz,
                    .referenced_names = {},
                    .saw_function_definition = false,
                    .source_names = {}
                };
                if (!expect(traverse_target_unit(unit.sections(), facts))) {
                    return;
                }
                declarations += facts.declarations;
                slices += facts.slices;
                empty_arrays += facts.empty_arrays;
                if (facts.slices != 0) {
                    const auto unique = std::flat_set<std::string>(
                        facts.referenced_names.begin(),
                        facts.referenced_names.end()
                    );
                    expect(unique.size() == facts.declarations);
                    expect(facts.slices > facts.declarations);
                }
            }
            expect(declarations == 4uz);
            expect(slices == 5uz);
            expect(empty_arrays == 1uz);
        };

    "Generation: static slice backing names are owned by their context module"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto inputs = std::vector<SourceModuleInput>();
            const auto append = [&](std::string_view name, std::string source) noexcept {
                const auto id =
                    sources.append_virtual(std::format("{}.cv", name), std::move(source));
                require(id.has_value());
                const auto path = CanonicalModulePath::from_value(name);
                require(path.has_value());
                inputs.push_back({.source_id = *id, .module_path = *path});
            };
            append(
                "provider",
                "export const numbers: [i32] = [1, 2]; fn provider_view() -> [i32] => numbers;"
            );
            append(
                "consumer",
                "import provider using numbers; fn consumer_view() -> [i32] => numbers;"
            );
            auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
            if (!expect(parsed.has_value())) {
                return;
            }
            auto analyzed = analyze(std::move(*parsed));
            if (!expect(analyzed.has_value())) {
                return;
            }
            const auto compilation = PlannedCompilation::build(
                std::move(analyzed->value),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("cross_artifact_slices")}
            );
            auto names = std::flat_set<std::string>();
            auto declarations = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto facts = StaticSliceFacts {
                    .unit = unit,
                    .declarations = 0uz,
                    .slices = 0uz,
                    .empty_arrays = 0uz,
                    .referenced_names = {},
                    .saw_function_definition = false,
                    .source_names = {}
                };
                if (!expect(traverse_target_unit(unit.sections(), facts))) {
                    return;
                }
                declarations += facts.declarations;
                for (const auto& name : facts.referenced_names) {
                    expect(names.insert(name).second);
                }
            }
            expect(declarations == 2uz);
            expect(names.size() == 2uz);
        };
});

} // namespace
