module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.freeze;

import :semantic.analysis.catalog;
import :semantic.analysis.constant.freeze;
import :semantic.analysis.construction;
import :semantic.analysis.lint.unused_imports;
import :semantic.analysis.program;
import :semantic.evaluation.value;
import :semantic.semir.constant_access;
import :test.internal.harness.death;
import :test.internal.semantic.evaluation.fixture;
import std;

TEST_CASE("Constant freezing: completed text survives program publication") {
    auto fixture = ConstantEvaluationFixture();
    auto& draft = fixture.compilation;
    const auto catalog = build_analysis_catalog(draft);
    REQUIRE(catalog.has_value());
    auto usage = ImportUsage(catalog->view().imports().size());
    REQUIRE(ProgramConstruction(draft, catalog->view(), usage).run().has_value());
    const auto frozen = freeze_constant_value(draft, ExecutionText(std::string("retained")));
    REQUIRE(frozen.has_value());
    CHECK(freeze_constant_value(draft, *frozen) == frozen);
    const auto program = std::move(draft).finish();
    REQUIRE(program.has_value());
    const auto reader = PublishedConstantValues(*program);
    CHECK(execution_text(reader, *frozen) == "retained");
}

TEST_CASE("Constant freezing invariant: retained constants belong to the receiving program") {
    CHECK(expect_termination("freeze-foreign-retained-constant", []() static noexcept {
        auto first = ConstantEvaluationFixture();
        auto second = ConstantEvaluationFixture();
        const auto foreign = second.compilation.intern_constant({
            .type = second.compilation.builtin_type(BuiltinType::Bool),
            .value = BooleanConstant {.value = true},
        });
        static_cast<void>(freeze_constant_value(first.compilation, foreign));
    }));
}
