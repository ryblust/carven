module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.control;

import :compilation.request;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.hir;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :source.manager;
import :source.module_path;
import std;

namespace {

auto control_module_path() noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value("control");
    REQUIRE(result.has_value());
    return std::move(*result);
}

auto analyze_errors(std::string source_text) noexcept -> Diagnostics {
    auto sources = SourceManager();
    const auto source_id = sources.append_virtual("control.cv", std::move(source_text));
    REQUIRE(source_id.has_value());
    const auto inputs = std::array {CompilationInput {
        .source_id = *source_id,
        .module_path = control_module_path(),
    }};
    auto parsed = parse(sources, inputs);
    REQUIRE(parsed.has_value());
    auto analyzed = analyze(std::move(*parsed));
    if (analyzed.has_value()) {
        return std::move(analyzed->diagnostics);
    }
    return std::move(analyzed.error());
}

auto analyze_program(std::string source_text) noexcept -> SemanticProgram {
    auto sources = SourceManager();
    const auto source_id = sources.append_virtual("control.cv", std::move(source_text));
    REQUIRE(source_id.has_value());
    const auto inputs = std::array {CompilationInput {
        .source_id = *source_id,
        .module_path = control_module_path(),
    }};
    auto parsed = parse(sources, inputs);
    REQUIRE(parsed.has_value());
    auto analyzed = analyze(std::move(*parsed));
    REQUIRE(analyzed.has_value());
    return std::move(analyzed->value);
}

auto contains_code(std::span<const Diagnostic> diagnostics, DiagnosticCode code) noexcept -> bool {
    return std::ranges::any_of(diagnostics, [&](const Diagnostic& diagnostic) noexcept {
        return diagnostic.finding.code == code;
    });
}

auto find_code(std::span<const Diagnostic> diagnostics, DiagnosticCode code) noexcept
    -> const Diagnostic* {
    const auto found = std::ranges::find_if(diagnostics, [&](const Diagnostic& diagnostic) {
        return diagnostic.finding.code == code;
    });
    return found == diagnostics.end() ? nullptr : &*found;
}

constexpr auto payload_prelude = "struct Payload { value: i32 }\n"
                                 "fn consume(&&payload: Payload) {}\n";

} // namespace

TEST_CASE("Semantic control: try around an infallible body is silent") {
    const auto diagnostics = analyze_errors("fn valid() { try {} catch {} }\n");
    CHECK(diagnostics.empty());
}

TEST_CASE("Semantic structure: nested lambda owns a distinct body") {
    const auto program = analyze_program(
        "private fn outer() {\n"
        "    let callback = []() { let nested = 1; };\n"
        "}\n"
    );
    REQUIRE_EQ(program.functions().size(), 1);
    REQUIRE_EQ(program.callables().size(), 2);
    REQUIRE_EQ(program.bodies().size(), 2);

    const auto outer_callable = program.function(FunctionID::from_index(0)).callable;
    const auto outer_body = program.callable(outer_callable).body;
    auto closure_callable = std::optional<CallableID>();
    for (const auto& expression : program.expressions()) {
        if (const auto* closure = std::get_if<HIRClosureExpr>(&expression.value)) {
            closure_callable = closure->callable;
        }
    }
    REQUIRE(closure_callable.has_value());
    const auto nested_body = program.callable(*closure_callable).body;
    CHECK_NE(outer_body, nested_body);
    CHECK_NE(program.body(outer_body).root, program.body(nested_body).root);
}

TEST_CASE("Semantic control: callable failure contracts have one explicit policy") {
    const auto program = analyze_program(
        "private fn inferred() {}\n"
        "fn published() {}\n"
        "struct Failure {}\n"
        "fn declared() throw Failure {}\n"
    );
    REQUIRE_EQ(program.functions().size(), 3);
    CHECK_EQ(
        program.callable(program.function(FunctionID::from_index(0)).callable).failure_contract,
        HIRFailureContractKind::Inferred
    );
    CHECK_EQ(
        program.callable(program.function(FunctionID::from_index(1)).callable).failure_contract,
        HIRFailureContractKind::UndeclaredPublished
    );
    CHECK_EQ(
        program.callable(program.function(FunctionID::from_index(2)).callable).failure_contract,
        HIRFailureContractKind::Declared
    );
}

TEST_CASE("Semantic failures: set identity is order-independent and all published facts use it") {
    const auto program = analyze_program(
        "struct AlphaFailure {}\n"
        "struct BetaFailure {}\n"
        "private fn alpha_first() throw AlphaFailure + BetaFailure {}\n"
        "private fn beta_first() throw BetaFailure + AlphaFailure {}\n"
        "private fn handled() {\n"
        "    try { alpha_first()?; } catch { AlphaFailure(_) => {}, BetaFailure(_) => {}, }\n"
        "}\n"
    );
    const auto first = program.function(FunctionID::from_index(0)).callable;
    const auto second = program.function(FunctionID::from_index(1)).callable;
    CHECK_EQ(program.callable(first).failure_set, program.callable(second).failure_set);

    for (auto index = 0uz; index < program.expressions().size(); ++index) {
        const auto id = HIRExprID::from_index(static_cast<std::uint32_t>(index));
        const auto& facts = program.expression_facts(id);
        CHECK_LT(facts.pending_failure_set.index(), program.failure_sets().size());
        CHECK_LT(facts.outward_failure_set.index(), program.failure_sets().size());
        CHECK_LT(facts.evaluation_failure_set.index(), program.failure_sets().size());
        auto evaluation = program.failure_set(facts.pending_failure_set).members;
        const auto& outward = program.failure_set(facts.outward_failure_set).members;
        evaluation.insert(evaluation.end(), outward.begin(), outward.end());
        std::ranges::sort(evaluation, {}, &HIRTypeID::index);
        evaluation.erase(std::ranges::unique(evaluation).begin(), evaluation.end());
        CHECK_EQ(evaluation, program.failure_set(facts.evaluation_failure_set).members);
        if (facts.attempt.has_value()) {
            CHECK_LT(facts.attempt->unhandled_failure_set.index(), program.failure_sets().size());
            for (const auto& arm : facts.attempt->arms) {
                CHECK_LT(arm.accepted_failure_set.index(), program.failure_sets().size());
            }
        }
    }
    for (auto index = 0uz; index < program.blocks().size(); ++index) {
        const auto id = HIRBlockID::from_index(static_cast<std::uint32_t>(index));
        CHECK_LT(
            program.block_facts(id).outward_failure_set.index(),
            program.failure_sets().size()
        );
    }
}

TEST_CASE("Semantic control: direct and mutually recursive inference reach one fixed point") {
    const auto program = analyze_program(
        "struct Failure {}\n"
        "private fn direct() { throw Failure {}; }\n"
        "private fn left(stop: bool) {\n"
        "    if stop { throw Failure {}; }\n"
        "    right(true)?;\n"
        "}\n"
        "private fn right(stop: bool) {\n"
        "    if stop { left(true)?; }\n"
        "}\n"
        "fn recover() {\n"
        "    try { direct()?; left(false)?; } catch { Failure(_) => {}, }\n"
        "}\n"
    );
    REQUIRE_EQ(program.functions().size(), 4);
    for (auto index = 0uz; index < 3; ++index) {
        const auto callable =
            program.function(FunctionID::from_index(static_cast<std::uint32_t>(index))).callable;
        CHECK_EQ(program.failure_set(program.callable(callable).failure_set).members.size(), 1);
    }
    auto recursive_handler_failure_set = std::optional<FailureSetID>();
    for (auto index = 0uz; index < program.expressions().size(); ++index) {
        const auto expression = HIRExprID::from_index(static_cast<std::uint32_t>(index));
        if (!std::holds_alternative<HIRTryExpr>(program.expression(expression).value)) {
            continue;
        }
        const auto& attempt = program.expression_facts(expression).attempt;
        if (attempt.has_value() && !attempt->arms.empty()) {
            recursive_handler_failure_set = attempt->arms.front().accepted_failure_set;
        }
    }
    REQUIRE(recursive_handler_failure_set.has_value());
    CHECK_EQ(program.failure_set(*recursive_handler_failure_set).members.size(), 1);
}

TEST_CASE("Semantic control: fixed contracts are dependency boundaries") {
    const auto program = analyze_program(
        "struct DeclaredFailure {}\n"
        "struct BodyFailure {}\n"
        "private fn source() { throw BodyFailure {}; }\n"
        "private fn fixed(fail: bool) throw DeclaredFailure {\n"
        "    if fail { throw DeclaredFailure {}; }\n"
        "    try { source()?; } catch { BodyFailure(_) => {}, }\n"
        "}\n"
        "private fn caller() { fixed(true)?; }\n"
    );
    REQUIRE_EQ(program.functions().size(), 3);
    const auto fixed = program.function(FunctionID::from_index(1)).callable;
    const auto caller = program.function(FunctionID::from_index(2)).callable;
    const auto fixed_failures = program.failure_set(program.callable(fixed).failure_set).members;
    const auto caller_failures = program.failure_set(program.callable(caller).failure_set).members;
    REQUIRE_EQ(fixed_failures.size(), 1);
    CHECK_EQ(caller_failures, fixed_failures);
}

TEST_CASE("Semantic availability: a loop backedge observes the second Take") {
    const auto diagnostics = analyze_errors(
        std::string(payload_prelude)
        + "fn invalid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    while true { consume(&&payload); }\n"
          "}\n"
    );
    CHECK(contains_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: a zero-iteration loop still joins with its Take path") {
    const auto diagnostics = analyze_errors(
        std::string(payload_prelude)
        + "fn invalid(run: bool) {\n"
          "    let payload = Payload { value: 1 };\n"
          "    while run { consume(&&payload); break; }\n"
          "    let observed = payload.value;\n"
          "}\n"
    );
    CHECK(contains_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: break and branch joins retain unavailable paths") {
    const auto break_diagnostics = analyze_errors(
        std::string(payload_prelude)
        + "fn invalid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    while true { consume(&&payload); break; }\n"
          "    let observed = payload.value;\n"
          "}\n"
    );
    CHECK(contains_code(break_diagnostics, DiagnosticCode::AccessUnavailable));

    const auto join_diagnostics = analyze_errors(
        std::string(payload_prelude)
        + "fn invalid(flag: bool) {\n"
          "    let payload = Payload { value: 1 };\n"
          "    if flag { consume(&&payload); }\n"
          "    let observed = payload.value;\n"
          "}\n"
    );
    CHECK(contains_code(join_diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: continue reaches the loop condition after Take") {
    const auto diagnostics = analyze_errors(
        std::string(payload_prelude)
        + "fn invalid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    while payload.value > 0 {\n"
          "        consume(&&payload);\n"
          "        continue;\n"
          "    }\n"
          "}\n"
    );
    CHECK(contains_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: continue reaches a c-style step before its condition") {
    const auto diagnostics = analyze_errors(
        std::string(payload_prelude)
        + "fn invalid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    for var index: i32 = 0; index < 1; consume(&&payload) {\n"
          "        consume(&&payload);\n"
          "        continue;\n"
          "    }\n"
          "}\n"
    );
    CHECK(contains_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: assignment target and RHS Take are one operation") {
    const auto diagnostics = analyze_errors(
        "struct Payload { value: i32 }\n"
        "fn relay(&&payload: Payload) -> Payload { return payload; }\n"
        "fn invalid() {\n"
        "    var payload = Payload { value: 1 };\n"
        "    payload = relay(&&payload);\n"
        "}\n"
    );
    CHECK(contains_code(diagnostics, DiagnosticCode::AccessOperationConflict));
}

TEST_CASE("Semantic availability: assignment restores an owner only after normal completion") {
    const auto restored = analyze_errors(
        std::string(payload_prelude)
        + "fn valid() {\n"
          "    var payload = Payload { value: 1 };\n"
          "    consume(&&payload);\n"
          "    payload = Payload { value: 2 };\n"
          "    let observed = payload.value;\n"
          "}\n"
    );
    CHECK_FALSE(contains_code(restored, DiagnosticCode::AccessUnavailable));

    const auto failure_path = analyze_errors(
        std::string(payload_prelude)
        + "struct Failure {}\n"
          "fn replacement() -> Payload throw Failure { throw Failure {}; }\n"
          "fn invalid() {\n"
          "    var payload = Payload { value: 1 };\n"
          "    consume(&&payload);\n"
          "    try { payload = replacement()?; } catch {\n"
          "        Failure(_) => { let observed = payload.value; },\n"
          "    }\n"
          "}\n"
    );
    CHECK(contains_code(failure_path, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: Write capture and Take conflict across a callable") {
    const auto diagnostics = analyze_errors(
        std::string(payload_prelude)
        + "fn invalid() {\n"
          "    var payload = Payload { value: 1 };\n"
          "    let callback = [&payload]() { payload.value = 2; };\n"
          "    consume(&&payload);\n"
          "}\n"
    );
    CHECK(contains_code(diagnostics, DiagnosticCode::AccessCaptureConflict));
}

TEST_CASE("Semantic availability: invocation failure edges retain Take state") {
    const auto diagnostics = analyze_errors(
        "struct Failure { code: i32 } struct Payload { value: i32 } "
        "fn fail(&&payload: Payload) throw Failure { "
        "throw Failure { code: payload.value }; } "
        "fn invalid(&&payload: Payload) { try { fail(&&payload)?; return; } "
        "catch { Failure(_) => {}, } let observed = payload.value; }"
    );
    CHECK(contains_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: false catch guards carry Take state to fallback") {
    const auto diagnostics = analyze_errors(
        std::string(payload_prelude)
        + "struct Failure { code: i32 }\n"
          "fn fail() throw Failure { throw Failure { code: 1 }; }\n"
          "fn reject(&&payload: Payload) -> bool { return false; }\n"
          "fn invalid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    try { fail()?; } catch {\n"
          "        Failure(_) if reject(&&payload) => {},\n"
          "        Failure(_) => { let observed = payload.value; },\n"
          "    }\n"
          "}\n"
    );
    CHECK(contains_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: rethrow carries Take state to an outer handler") {
    const auto diagnostics = analyze_errors(
        std::string(payload_prelude)
        + "struct Failure { code: i32 }\n"
          "fn fail() throw Failure { throw Failure { code: 1 }; }\n"
          "fn invalid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    try {\n"
          "        try { fail()?; } catch {\n"
          "            Failure(_) => { consume(&&payload); rethrow; },\n"
          "        }\n"
          "    } catch {\n"
          "        Failure(_) => { let observed = payload.value; },\n"
          "    }\n"
          "}\n"
    );
    CHECK(contains_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: multi-word place state converges through a loop") {
    auto source = std::string(payload_prelude);
    source += "fn invalid(run: bool) {\n";
    for (auto index = 0; index < 70; ++index) {
        source += std::format("    let payload{} = Payload {{ value: {} }};\n", index, index);
    }
    source += "    while run { consume(&&payload69); break; }\n"
              "    let observed = payload69.value;\n"
              "}\n";
    const auto diagnostics = analyze_errors(std::move(source));
    CHECK(contains_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: unreachable operations do not diagnose") {
    const auto diagnostics = analyze_errors(
        std::string(payload_prelude)
        + "fn valid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    return;\n"
          "    consume(&&payload);\n"
          "    let observed = payload.value;\n"
          "}\n"
    );
    CHECK_FALSE(contains_code(diagnostics, DiagnosticCode::AccessUnavailable));
    CHECK_FALSE(contains_code(diagnostics, DiagnosticCode::AccessOperationConflict));
}

TEST_CASE("Semantic availability: joins choose the earliest structural Take witness") {
    const auto source = std::string(payload_prelude)
        + "fn invalid(flag: bool) {\n"
          "    let payload = Payload { value: 1 };\n"
          "    if flag { consume(&&payload); }\n"
          "    else { consume(&&payload); }\n"
          "    let observed = payload.value;\n"
          "}\n";
    const auto function_start = source.find("fn invalid");
    const auto first_take = source.find("&&payload", function_start);
    const auto second_take = source.find("&&payload", first_take + 1);
    REQUIRE_NE(first_take, std::string::npos);
    REQUIRE_NE(second_take, std::string::npos);
    const auto diagnostics = analyze_errors(source);
    const auto* unavailable = find_code(diagnostics, DiagnosticCode::AccessUnavailable);
    REQUIRE(unavailable != nullptr);
    REQUIRE_FALSE(unavailable->attachment.related.empty());
    const auto witness = unavailable->attachment.related.front().span.span.start();
    CHECK_GE(witness, first_take);
    CHECK_LT(witness, second_take);
}

TEST_CASE("Semantic availability: closure-local places are isolated from the outer body") {
    const auto diagnostics = analyze_errors(
        std::string(payload_prelude)
        + "fn valid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    let callback = []() {\n"
          "        let nested = Payload { value: 2 };\n"
          "        consume(&&nested);\n"
          "    };\n"
          "    let observed = payload.value;\n"
          "}\n"
    );
    CHECK_FALSE(contains_code(diagnostics, DiagnosticCode::AccessUnavailable));
}
