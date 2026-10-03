module carven:test.internal.backend.generation.failure_receivers;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target.type;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

struct ReceiverQuery final {
    const TargetUnit& unit;
    std::size_t slots;
    std::size_t outcomes;
    std::size_t deferred;
    std::size_t handlers;
    std::size_t locals;
    std::size_t bodies;
    bool measured;

    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool;
    auto leave_declaration(const TargetDecl& declaration) noexcept -> bool;
    auto visit_variable(const TargetVariableStmt& variable) noexcept -> bool;
    auto enter_expression(const TargetExpr& expression, TargetExpressionRole role) noexcept -> bool;
};

auto ReceiverQuery::enter_declaration(const TargetDecl& declaration) noexcept -> bool {
    const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
    measured = function != nullptr
        && function->name.components().back().spelling() == "probe"
        && std::holds_alternative<TargetFreeFunctionDefinition>(function->form);
    bodies += measured;
    return true;
}

auto ReceiverQuery::leave_declaration(const TargetDecl&) noexcept -> bool {
    measured = false;
    return true;
}

auto ReceiverQuery::visit_variable(const TargetVariableStmt& variable) noexcept -> bool {
    if (!measured) {
        return true;
    }
    ++locals;
    if (const auto* type = std::get_if<TargetIntrinsicType>(&unit.type(variable.type).value)) {
        slots += type->symbol == TargetSymbol::StdOptional;
        outcomes += type->symbol == TargetSymbol::RuntimeOutcome;
        deferred += type->symbol == TargetSymbol::RuntimeDeferredResult;
    }
    return true;
}

auto ReceiverQuery::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    if (!measured) {
        return true;
    }
    if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
        if (const auto* name =
                std::get_if<TargetNameExpr>(&template_primary_expression(*call->callee).value)) {
            handlers += name->name.components().back().spelling() == "handled";
        }
    }
    return true;
}

const ct::Suite tests([] static noexcept {
    ct::test("Generation: failure receivers use sources or necessary joins", [] static noexcept {
        struct Case final {
            std::string_view name;
            std::string_view body;
            std::size_t slots;
            std::size_t outcomes;
        };
        const auto cases = std::array {
            Case {
                .name = "single call tail",
                .body = "return try { source(flag)? } catch { Error(_) => handled(), };",
                .slots = 0uz,
                .outcomes = 1uz
            },
            Case {
                .name = "single call followed by computation",
                .body = "return try { let value = source(flag)?; value + 2 } "
                        "catch { Error(_) => handled(), };",
                .slots = 0uz,
                .outcomes = 1uz
            },
            Case {
                .name = "conditionally executed source",
                .body = "return try { (if flag { source(flag)? } else { 9 }) } "
                        "catch { Error(_) => handled(), };",
                .slots = 0uz,
                .outcomes = 1uz
            },
            Case {
                .name = "direct throw",
                .body = "return try { throw Error { 3 }; } catch { Error(_) => handled(), };",
                .slots = 0uz,
                .outcomes = 0uz
            },
            Case {
                .name = "one carrier with multiple alternatives",
                .body = "return try { wide(flag)? } catch { _ => handled(), };",
                .slots = 0uz,
                .outcomes = 1uz
            },
            Case {
                .name = "two calls join before one handler",
                .body = "return try { source(flag)? + source(!flag)? } "
                        "catch { Error(_) => handled(), };",
                .slots = 1uz,
                .outcomes = 2uz
            },
            Case {
                .name = "two throws join before one handler",
                .body = "return try { if flag { throw Error { 1 }; } "
                        "else { throw Error { 2 }; } } catch { Error(_) => handled(), };",
                .slots = 1uz,
                .outcomes = 0uz
            },
            Case {
                .name = "protected owner must clean before handler",
                .body = "return try { let text = \"owned\" as String; "
                        "source(flag)? + (text.len() as i32) } "
                        "catch { Error(_) => handled(), };",
                .slots = 1uz,
                .outcomes = 1uz
            },
            Case {
                .name = "owner constructed after failure does not constrain the handoff",
                .body = "return try { let value = source(flag)?; "
                        "let text = \"owned\" as String; value + (text.len() as i32) } "
                        "catch { Error(_) => handled(), };",
                .slots = 0uz,
                .outcomes = 1uz
            },
            Case {
                .name = "owning failure retains transport",
                .body = "return try { text_source(flag)? } "
                        "catch { TextError(_) => handled(), };",
                .slots = 1uz,
                .outcomes = 1uz
            },
        };
        ct::each(cases, &Case::name, [](const Case& input) static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    std::format(
                        "struct Error {{ code: i32 }} "
                        "struct Other {{}} "
                        "struct TextError {{ text: String }} "
                        "fn source(flag: bool) -> i32 throw Error {{ "
                        "if flag {{ throw Error {{ 7 }}; }} return 5; }} "
                        "fn wide(flag: bool) -> i32 throw Error + Other {{ "
                        "if flag {{ throw Error {{ 7 }}; }} throw Other {{}}; }} "
                        "fn text_source(flag: bool) -> i32 throw TextError {{ "
                        "if flag {{ throw TextError {{ \"failure\" as String }}; }} return 5; }} "
                        "fn handled() -> i32 => 11; "
                        "fn probe(flag: bool) -> i32 {{ {} }}",
                        input.body
                    )
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("failure_receivers")}
            );
            auto slots = 0uz;
            auto outcomes = 0uz;
            auto deferred = 0uz;
            auto handlers = 0uz;
            auto bodies = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = ReceiverQuery {
                    .unit = unit,
                    .slots = 0uz,
                    .outcomes = 0uz,
                    .deferred = 0uz,
                    .handlers = 0uz,
                    .locals = 0uz,
                    .bodies = 0uz,
                    .measured = false
                };
                if (!ct::expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
                slots += query.slots;
                outcomes += query.outcomes;
                deferred += query.deferred;
                handlers += query.handlers;
                bodies += query.bodies;
            }
            ct::expect_equal(bodies, 1uz);
            ct::expect_equal(slots, input.slots);
            ct::expect_equal(outcomes, input.outcomes);
            ct::expect_equal(deferred, 0uz);
            ct::expect_equal(handlers, 1uz);
        });
    });

    ct::test(
        "Generation: unused direct failure payloads retain execution without storage",
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
                struct Error { code: i32 }
                fn payload(&trace: i32) -> Error {
                    trace += 1;
                    return Error { 7 };
                }
                fn handled(&trace: i32) -> i32 { trace += 10; return 3; }
                fn probe(&trace: i32) -> i32 => try {
                    throw payload(&trace);
                } catch { Error(_) => handled(&trace), };
            )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("unused_failure_payload")}
            );
            auto locals = 0uz;
            auto handlers = 0uz;
            auto bodies = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = ReceiverQuery {
                    .unit = unit,
                    .slots = 0uz,
                    .outcomes = 0uz,
                    .deferred = 0uz,
                    .handlers = 0uz,
                    .locals = 0uz,
                    .bodies = 0uz,
                    .measured = false
                };
                ct::expect(traverse_target_unit(unit.sections(), query));
                locals += query.locals;
                handlers += query.handlers;
                bodies += query.bodies;
            }
            ct::expect_equal(bodies, 1uz);
            ct::expect_equal(locals, 0uz);
            ct::expect_equal(handlers, 1uz);
        }
    );
});

} // namespace
