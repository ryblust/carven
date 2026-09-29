module carven:test.internal.semantic.analysis.freeze;

import :semantic.analysis.catalog;
import :semantic.analysis.constant.freeze;
import :semantic.analysis.construction;
import :semantic.analysis.lint.unused_imports;
import :semantic.analysis.program;
import :semantic.evaluation.value;
import :semantic.semir.constant_access;
import :test.harness.framework;
import :test.internal.harness.death;
import :test.internal.semantic.evaluation.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Constant freezing: completed text survives program publication", [] static noexcept {
        auto fixture = ConstantEvaluationFixture();
        auto& draft = fixture.compilation;
        const auto catalog = build_analysis_catalog(draft);
        if (!ct::expect(catalog.has_value())) {
            return;
        }
        auto usage = ImportUsage(catalog->view().imports().size());
        if (!ct::expect(ProgramConstruction(draft, catalog->view(), usage).run().has_value())) {
            return;
        }
        const auto frozen = freeze_constant_value(draft, ExecutionText(std::string("retained")));
        if (!ct::expect(frozen.has_value())) {
            return;
        }
        ct::expect(freeze_constant_value(draft, *frozen) == frozen);
        const auto program = std::move(draft).finish();
        if (!ct::expect(program.has_value())) {
            return;
        }
        const auto reader = PublishedConstantValues(*program);
        ct::expect(execution_text(reader, *frozen) == "retained");
    });

    ct::test(
        "Constant freezing invariant: retained constants belong to the receiving program",
        [] static noexcept {
            auto first = ConstantEvaluationFixture();
            auto second = ConstantEvaluationFixture();
            const auto foreign = second.compilation.intern_constant({
                .type = second.compilation.builtin_type(BuiltinType::Bool),
                .value = BooleanConstant {.value = true},
            });
            ct::expect(expect_termination("freeze-foreign-retained-constant", [&]() noexcept {
                static_cast<void>(freeze_constant_value(first.compilation, foreign));
            }));
        }
    );
});

} // namespace
