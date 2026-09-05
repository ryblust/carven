module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.generation.artifact_contracts;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :compiler.request;
import :diagnostics.code;
import :source.manager;
import :source.module_path;
import :source.text;
import std;

namespace {

template<typename Result>
auto checked_artifacts(Result result) noexcept -> GeneratedArtifactSet {
    auto diagnostic_report = std::string();
    if (!result.has_value()) {
        for (const auto& diagnostic : result.error()) {
            diagnostic_report += std::format(
                "{}: {}\n",
                diagnostic_code_info(diagnostic.finding.code).name,
                diagnostic.finding.message
            );
        }
    }
    INFO(diagnostic_report);
    REQUIRE(result.has_value());
    if (!result.has_value()) {
        return GeneratedArtifactSet(std::vector<GeneratedArtifact> {});
    }
    return std::move(result->value);
}

auto compile_artifact_fixture() noexcept -> GeneratedArtifactSet {
    auto sources = SourceManager();
    const auto source = sources.append_virtual(
        "quality.cv",
        "import \"quality_provider.hpp\";\n"
        "\n"
        "private import(cpp) fn cv_quality_record(marker: i32) -> i32;\n"
        "\n"
        "private struct Hidden {\n"
        "    value: i32,\n"
        "}\n"
        "\n"
        "struct PairData {\n"
        "    left: i32,\n"
        "    right: i32,\n"
        "}\n"
        "\n"
        "private fn hidden_value(value: Hidden) -> i32 {\n"
        "    return value.value;\n"
        "}\n"
        "\n"
        "enum Status: i32 {\n"
        "    Ready = 0,\n"
        "    Failed = 1,\n"
        "}\n"
        "\n"
        "enum Result {\n"
        "    Number(i32),\n"
        "    Empty,\n"
        "}\n"
        "\n"
        "enum Collision {\n"
        "    Storage,\n"
        "    storage,\n"
        "    Number(i32),\n"
        "    NumberPayload,\n"
        "    is_number,\n"
        "    as_number,\n"
        "}\n"
        "\n"
        "enum DeadChoice {\n"
        "    First,\n"
        "    Second,\n"
        "    Third,\n"
        "}\n"
        "\n"
        "fn pair(left: i32, right: i32) -> i32 {\n"
        "    return left * 10 + right;\n"
        "}\n"
        "\n"
        "fn identity(value: i32) -> i32 {\n"
        "    return value;\n"
        "}\n"
        "\n"
        "fn ordered_callee() -> i32 {\n"
        "    return (if cv_quality_record(6) > 0 {\n"
        "        identity\n"
        "    } else {\n"
        "        identity\n"
        "    })(cv_quality_record(7));\n"
        "}\n"
        "\n"
        "fn pure(first: i32, second: i32) -> i32 {\n"
        "    return pair(first + 1, second + 2);\n"
        "}\n"
        "\n"
        "fn reordered(first: i32, second: i32) -> PairData {\n"
        "    return PairData { right: second, left: first };\n"
        "}\n"
        "\n"
        "fn reordered_effects() -> PairData {\n"
        "    return PairData {\n"
        "        right: cv_quality_record(4),\n"
        "        left: cv_quality_record(5),\n"
        "    };\n"
        "}\n"
        "\n"
        "fn ordered() -> i32 {\n"
        "    return pair(\n"
        "        cv_quality_record(1),\n"
        "        cv_quality_record(2),\n"
        "    );\n"
        "}\n"
        "\n"
        "fn source_first(operand: i32) -> i32 {\n"
        "    return pair(cv_quality_record(3), operand);\n"
        "}\n"
        "\n"
        "fn reserved_name(__value: i32) -> i32 {\n"
        "    return __value;\n"
        "}\n"
        "\n"
        "fn root_scope_collision(class: i32) -> i32 {\n"
        "    let class_cv: i32 = 1;\n"
        "    return class + class_cv;\n"
        "}\n"
        "\n"
        "fn ignore(unused_value: i32) -> i32 {\n"
        "    return 7;\n"
        "}\n"
        "\n"
        "fn fallible_outcome() -> i32 {\n"
        "    return 0;\n"
        "}\n"
        "\n"
        "fn fallible() -> i32 throw Status {\n"
        "    throw Status::Failed;\n"
        "}\n"
        "\n"
        "fn recover_status() -> i32 {\n"
        "    return try {\n"
        "        fallible()?\n"
        "    } catch {\n"
        "        Status(.Ready) if true => 1,\n"
        "        Status(.Failed) => 2,\n"
        "        Status(.Ready) => 3,\n"
        "    };\n"
        "}\n"
        "\n"
        "struct QualityFailure { code: i32 }\n"
        "\n"
        "struct AlternateFailure { code: i32 }\n"
        "\n"
        "fn fail_quality() -> i32 throw QualityFailure {\n"
        "    throw QualityFailure { code: 29 };\n"
        "}\n"
        "\n"
        "fn fail_alternate(alternate: bool) -> i32 throw QualityFailure + AlternateFailure {\n"
        "    if alternate {\n"
        "        throw AlternateFailure { code: 31 };\n"
        "    }\n"
        "    throw QualityFailure { code: 29 };\n"
        "}\n"
        "\n"
        "fn preserve_alternate(alternate: bool) -> i32 throw AlternateFailure {\n"
        "    return try {\n"
        "        fail_alternate(alternate)?\n"
        "    } catch {\n"
        "        QualityFailure(_) => 0,\n"
        "    };\n"
        "}\n"
        "\n"
        "fn preserve_quality_failure() -> i32 throw QualityFailure {\n"
        "    return try {\n"
        "        fail_quality()?\n"
        "    } catch {\n"
        "        QualityFailure(error) => rethrow,\n"
        "    };\n"
        "}\n"
        "\n"
        "fn recover_status_alternatives() -> i32 {\n"
        "    return try {\n"
        "        fallible()?\n"
        "    } catch {\n"
        "        Status(.Ready) | Status(.Failed) => 4,\n"
        "    };\n"
        "}\n"
        "\n"
        "fn increment(&value: i32) {\n"
        "    value += 1;\n"
        "}\n"
        "\n"
        "fn write_pair(&left: i32, &right: i32) {\n"
        "    left = 1;\n"
        "    right = 2;\n"
        "}\n"
        "\n"
        "fn stable_writes(&value: i32) {\n"
        "    write_pair(&value, &value);\n"
        "}\n"
        "\n"
        "fn indexed(values: [i32; 2], index: i32) -> i32 {\n"
        "    return values[index];\n"
        "}\n"
        "\n"
        "fn mutate_index(&values: [i32; 2], index: i32) {\n"
        "    values[index] += 1;\n"
        "}\n"
        "\n"
        "fn ordered_mutation(&values: [i32; 2]) {\n"
        "    values[cv_quality_record(8)] += cv_quality_record(9);\n"
        "}\n"
        "\n"
        "fn guarded_irrefutable(value: i32) -> i32 {\n"
        "    return match value {\n"
        "        candidate if candidate == 0 => cv_quality_record(13),\n"
        "        1 => cv_quality_record(14),\n"
        "        _ => cv_quality_record(15),\n"
        "    };\n"
        "}\n"
        "\n"
        "fn classify(value: Result) -> i32 {\n"
        "    return match value {\n"
        "        .Number(number) => number,\n"
        "        .Empty => 0,\n"
        "    };\n"
        "}\n"
        "\n"
        "fn guarded_classify(value: Result) -> i32 {\n"
        "    return match value {\n"
        "        .Number(number) if number > 0 => number,\n"
        "        .Number(_) => 0,\n"
        "        .Empty => -1,\n"
        "    };\n"
        "}\n"
        "\n"
        "fn covered_union_arm(value: DeadChoice) -> i32 {\n"
        "    return match value {\n"
        "        .First | .Second => 1,\n"
        "        .First if true => {\n"
        "                let cv_dead_union_body = 2;\n"
        "                cv_dead_union_body\n"
        "            },\n"
        "        .Third => 3,\n"
        "    };\n"
        "}\n"
        "\n"
        "fn covered_subject_consumer() {\n"
        "    match cv_quality_record(12) == 12 {\n"
        "        _ => {},\n"
        "        cv_dead_pattern_binding if false => {\n"
        "                let cv_dead_subject_body = 0;\n"
        "            },\n"
        "    }\n"
        "}\n"
        "\n"
        "fn count() -> i32 {\n"
        "    var total: i32 = 0;\n"
        "    for var index: i32 = 0; index < 3; ++index {\n"
        "        total += index;\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "\n"
        "fn scoped_source_name() -> i32 {\n"
        "    if true {\n"
        "        let operand: i32 = 0;\n"
        "    }\n"
        "    return pair(\n"
        "        cv_quality_record(10),\n"
        "        cv_quality_record(11),\n"
        "    );\n"
        "}\n"
        "\n"
        "fn declaration_shadow(value: i32) -> i32 {\n"
        "    if true {\n"
        "        let value = value;\n"
        "        return value;\n"
        "    }\n"
        "    return 0;\n"
        "}\n"
    );
    REQUIRE(source.has_value());
    const auto path = CanonicalModulePath::from_value("quality");
    REQUIRE(path.has_value());
    const auto input = CompilationModuleInput {.source_id = *source, .module_path = *path};
    auto result = compile(
        sources,
        CompilationRequest {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:quality").value(),
        }
    );
    return checked_artifacts(std::move(result));
}

auto compile_opaque_raw_fixture() noexcept -> GeneratedArtifactSet {
    auto sources = SourceManager();
    const auto source = sources.append_virtual(
        "opaque.cv",
        "#[cpp] ---\n"
        "inline constexpr auto cv_raw_source = R\"(#line CARVEN_SOURCE_LINE 7 \\\"raw.cv\\\")\";\n"
        "---\n"
        "\n"
        "\n"
        "#[cpp] -----\n"
        "inline constexpr auto cv_raw_generated = R\"(#line CARVEN_GENERATED_LINE \\\"raw.cpp\\\")\";\n"
        "-----\n"
    );
    REQUIRE(source.has_value());
    const auto path = CanonicalModulePath::from_value("opaque");
    REQUIRE(path.has_value());
    const auto input = CompilationModuleInput {.source_id = *source, .module_path = *path};
    auto result = compile(
        sources,
        CompilationRequest {.modules = std::span(&input, 1)},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:quality").value(),
        }
    );
    return checked_artifacts(std::move(result));
}

auto artifact_content(const GeneratedArtifactSet& artifacts, std::string_view path) noexcept
    -> std::string_view {
    const auto found =
        std::ranges::find(artifacts.artifacts(), path, &GeneratedArtifact::logical_path);
    REQUIRE(found != artifacts.artifacts().end());
    if (found == artifacts.artifacts().end()) {
        return {};
    }
    return found->content;
}

auto fixture_artifacts() noexcept -> const GeneratedArtifactSet& {
    static const auto artifacts = compile_artifact_fixture();
    return artifacts;
}

auto occurrence_count(std::string_view text, std::string_view needle) noexcept -> std::size_t {
    auto count = 0uz;
    for (auto position = text.find(needle); position != std::string_view::npos;
         position = text.find(needle, position + needle.size())) {
        ++count;
    }
    return count;
}

} // namespace

TEST_CASE("Generated artifacts: artifacts retain source attribution and exact dependencies") {
    const auto& artifacts = fixture_artifacts();
    const auto implementation = artifact_content(artifacts, "quality.cpp");

    CHECK(implementation.contains("\"quality.cv\""));
    CHECK_FALSE(implementation.contains("#include <carven/runtime/runtime.hpp>"));
    CHECK(implementation.contains("#include <carven/runtime/array.hpp>"));
    CHECK(implementation.contains("#include <carven/runtime/callable.hpp>"));
    CHECK(implementation.contains("#include <carven/runtime/numeric.hpp>"));
    CHECK(implementation.contains("#include <carven/runtime/outcome.hpp>"));
    CHECK_FALSE(implementation.contains("#include <carven/runtime/entry.hpp>"));
    CHECK_FALSE(implementation.contains("#include <carven/runtime/text.hpp>"));
    CHECK(implementation.contains("#include \"quality_provider.hpp\""));
}

TEST_CASE("Generated artifacts: raw fragments preserve line-marker-shaped bytes") {
    const auto artifacts = compile_opaque_raw_fixture();
    const auto implementation = artifact_content(artifacts, "opaque.cpp");

    CHECK(implementation.contains("R\"(#line CARVEN_SOURCE_LINE 7 \\\"raw.cv\\\")\""));
    CHECK(implementation.contains("R\"(#line CARVEN_GENERATED_LINE \\\"raw.cpp\\\")\""));
    CHECK(implementation.find("cv_raw_source") < implementation.find("cv_raw_generated"));
    CHECK_GE(occurrence_count(implementation, "\"opaque.cv\""), 2u);
}
