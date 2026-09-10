module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.construction.construction;

import :backend.construction;
import :backend.construction.verify;
import :semantic.semir;
import :test.internal.backend.construction.fixture;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {
template<typename Edit>
void reject_construction(
    std::string_view text,
    Edit edit,
    ConstructionViolationKind kind
) noexcept {
    const auto semantic = analyze_test_program(std::string(text));
    auto checked = false;
    for (const auto entry : semantic.bodies().entries()) {
        auto construction = construct_body(semantic, entry.id);
        if (!edit(construction, semantic)) {
            continue;
        }
        const auto result = validate_construction(construction, entry.value);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == kind);
        CHECK(result.error().origin.has_value());
        checked = true;
    }
    CHECK(checked);
}
} // namespace

TEST_CASE("Construction: expression references reject foreign and out of range IDs") {
    for (const auto foreign : {false, true}) {
        reject_construction(
            "fn first(n: i32) -> i32 { return n + 1; } fn second() {}",
            [=](BodyConstruction& construction, const SemIRProgram& semantic) noexcept {
                for (auto& expression : ConstructionTestingFixture::expressions(construction)) {
                    if (auto* operation = std::get_if<ConstructionOperation>(&expression.value);
                        operation != nullptr
                        && std::holds_alternative<SemBinary>(expression.operation.value)) {
                        auto& inputs = operation->operands;
                        auto owner = construction.body();
                        if (foreign) {
                            for (const auto entry : semantic.bodies().entries()) {
                                if (entry.id != owner) {
                                    owner = entry.id;
                                    break;
                                }
                            }
                        }
                        inputs[0].expression = ConstructionTestingFixture::expression_id(
                            owner,
                            foreign ? 0u : std::numeric_limits<std::uint32_t>::max()
                        );
                        return true;
                    }
                }
                return false;
            },
            ConstructionViolationKind::InvalidIdentity
        );
    }
}

TEST_CASE("Construction: execution occurrences reject duplication and cycles") {
    for (const auto cycle : {false, true}) {
        reject_construction(
            "fn probe(n: i32) -> i32 { return n + 1; }",
            [=](BodyConstruction& construction, const SemIRProgram&) noexcept {
                const auto rows = ConstructionTestingFixture::expressions(construction);
                for (auto index = 0uz; index < rows.size(); ++index) {
                    if (auto* operation = std::get_if<ConstructionOperation>(&rows[index].value);
                        operation != nullptr
                        && std::holds_alternative<SemBinary>(rows[index].operation.value)) {
                        auto& inputs = operation->operands;
                        inputs[1].expression = cycle ? ConstructionTestingFixture::expression_id(
                                                           construction.body(),
                                                           static_cast<std::uint32_t>(index)
                                                       )
                                                     : inputs[0].expression;
                        return true;
                    }
                }
                return false;
            },
            cycle ? ConstructionViolationKind::ExecutionCycle
                  : ConstructionViolationKind::DuplicateExecution
        );
    }
}

TEST_CASE("Construction: pattern references belong to the current semantic body") {
    reject_construction(
        "fn first(n: i32) -> i32 { return match n { _ => n }; } "
        "fn second(n: i32) -> i32 { return match n { _ => n }; }",
        [](BodyConstruction& construction, const SemIRProgram& semantic) static noexcept {
            for (auto& expression : ConstructionTestingFixture::expressions(construction)) {
                auto* match = std::get_if<ConstructionMatch>(&expression.value);
                if (match == nullptr) {
                    continue;
                }
                for (const auto entry : semantic.bodies().entries()) {
                    if (entry.id == construction.body()) {
                        continue;
                    }
                    for (const auto pattern : entry.value.patterns()) {
                        match->arms.front().pattern_id = pattern.id;
                        return true;
                    }
                }
            }
            return false;
        },
        ConstructionViolationKind::InvalidIdentity
    );
}

TEST_CASE("Construction: region execution rejects invalid references duplication and cycles") {
    for (const auto kind :
         {ConstructionViolationKind::InvalidIdentity,
          ConstructionViolationKind::DuplicateExecution,
          ConstructionViolationKind::ExecutionCycle}) {
        reject_construction(
            "fn probe(flag: bool) { while flag { break; } }",
            [=](BodyConstruction& construction, const SemIRProgram&) noexcept {
                auto& root =
                    ConstructionTestingFixture::regions(construction)[construction.root().index()];
                if (root.statements.empty()) {
                    return false;
                }
                auto* scope = std::get_if<ConstructionScope>(&root.statements.front().value);
                if (scope == nullptr) {
                    return false;
                }
                if (kind == ConstructionViolationKind::DuplicateExecution) {
                    root.statements.push_back(root.statements.front());
                } else {
                    scope->region = kind == ConstructionViolationKind::ExecutionCycle
                        ? construction.root()
                        : ConstructionTestingFixture::region_id(
                              construction.body(),
                              std::numeric_limits<std::uint32_t>::max()
                          );
                }
                return true;
            },
            kind
        );
    }
}

TEST_CASE("Construction: unreachable occurrences and foreign lifetimes are rejected") {
    reject_construction(
        "fn first(n: i32) -> i32 { return n + 1; }",
        [](BodyConstruction& construction, const SemIRProgram&) static noexcept {
            if (construction.expression_values().empty()) {
                return false;
            }
            ConstructionTestingFixture::regions(construction)[construction.root().index()]
                .statements.clear();
            return true;
        },
        ConstructionViolationKind::UnownedExecution
    );
    reject_construction(
        "fn first(n: i32) -> i32 { return n + 1; } fn second() {}",
        [](BodyConstruction& construction, const SemIRProgram& semantic) static noexcept {
            if (construction.expression_values().empty()) {
                return false;
            }
            for (const auto entry : semantic.bodies().entries()) {
                if (entry.id != construction.body()) {
                    ConstructionTestingFixture::expressions(construction).front().lifetime =
                        entry.value.region().lifetime;
                    return true;
                }
            }
            return false;
        },
        ConstructionViolationKind::InvalidLifetime
    );
}

TEST_CASE("Construction: loop references must identify the enclosing loop body") {
    for (const auto wrong_kind : {false, true}) {
        reject_construction(
            "fn probe(flag: bool) { while flag { while flag { break; } break; } }",
            [=](BodyConstruction& construction, const SemIRProgram&) noexcept {
                auto loops = std::vector<ConstructionRegionID>();
                const auto regions = ConstructionTestingFixture::regions(construction);
                for (auto index = 0uz; index < regions.size(); ++index) {
                    for (const auto& statement : regions[index].statements) {
                        if (std::holds_alternative<ConstructionLoop>(statement.value)) {
                            loops.push_back(
                                ConstructionTestingFixture::region_id(
                                    construction.body(),
                                    static_cast<std::uint32_t>(index)
                                )
                            );
                        }
                    }
                }
                if (loops.size() != 2uz) {
                    return false;
                }
                for (auto& region : regions) {
                    for (auto& statement : region.statements) {
                        if (auto* transfer =
                                std::get_if<ConstructionLoopTransfer>(&statement.value)) {
                            transfer->loop = wrong_kind           ? construction.root()
                                : transfer->loop == loops.front() ? loops.back()
                                                                  : loops.front();
                            return true;
                        }
                    }
                }
                return false;
            },
            ConstructionViolationKind::InvalidControl
        );
    }
}

TEST_CASE("Construction: catch failures and rethrows cannot return to their own handler") {
    for (const auto bad_source : {false, true}) {
        reject_construction(
            "struct Error {} fn fail() throw Error { throw Error {}; } "
            "fn probe() throw Error { try { fail()?; } catch { Error(_) => rethrow, } }",
            [=](BodyConstruction& construction, const SemIRProgram&) noexcept {
                for (auto& region : ConstructionTestingFixture::regions(construction)) {
                    for (auto& statement : region.statements) {
                        if (auto* rethrow = std::get_if<ConstructionRethrow>(&statement.value)) {
                            if (bad_source) {
                                // The body's callee occurrence has a valid ID but is not a selected catch.
                                for (auto index = 0uz;
                                     index < construction.expression_values().size();
                                     ++index) {
                                    if (std::holds_alternative<SemCallable>(
                                            construction.expression_values()[index].operation.value
                                        )) {
                                        rethrow->source.handler =
                                            ConstructionTestingFixture::expression_id(
                                                construction.body(),
                                                static_cast<std::uint32_t>(index)
                                            );
                                        break;
                                    }
                                }
                            } else {
                                rethrow->destination =
                                    ConstructionHandlerExit {rethrow->source.handler};
                            }
                            return true;
                        }
                    }
                }
                return false;
            },
            ConstructionViolationKind::InvalidControl
        );
    }
}

TEST_CASE("Construction: failure references reject nonhandler targets") {
    reject_construction(
        "struct Error {} fn fail() throw Error { throw Error {}; } fn probe() throw Error { fail()?; }",
        [](BodyConstruction& construction, const SemIRProgram&) static noexcept {
            for (auto& expression : ConstructionTestingFixture::expressions(construction)) {
                if (auto* call = std::get_if<ConstructionOperation>(&expression.value);
                    call != nullptr && call->failure) {
                    call->failure->destination =
                        ConstructionHandlerExit {call->operands.front().expression};
                    return true;
                }
            }
            return false;
        },
        ConstructionViolationKind::InvalidControl
    );
}

TEST_CASE("Construction: failures in a catch body bypass that handler") {
    reject_construction(
        "struct Error {} fn fail() throw Error { throw Error {}; } "
        "fn probe() throw Error { try { fail()?; } catch { Error(_) => { fail()?; }, } }",
        [](BodyConstruction& construction, const SemIRProgram&) static noexcept {
            const auto expressions = ConstructionTestingFixture::expressions(construction);
            for (auto index = 0uz; index < expressions.size(); ++index) {
                const auto* handler = std::get_if<ConstructionTry>(&expressions[index].value);
                if (handler == nullptr || handler->arms.empty()) {
                    continue;
                }
                const auto& statements = construction.region(handler->arms.front().body).statements;
                if (statements.empty()) {
                    continue;
                }
                const auto* discard = std::get_if<ConstructionDiscard>(&statements.front().value);
                if (discard == nullptr) {
                    continue;
                }
                auto* call = std::get_if<ConstructionOperation>(
                    &expressions[discard->expression.index()].value
                );
                if (call == nullptr || !call->failure) {
                    continue;
                }
                call->failure->destination =
                    ConstructionHandlerExit {ConstructionTestingFixture::expression_id(
                        construction.body(),
                        static_cast<std::uint32_t>(index)
                    )};
                return true;
            }
            return false;
        },
        ConstructionViolationKind::InvalidControl
    );
}

TEST_CASE("Construction: loop steps cannot target the loop whose body has ended") {
    reject_construction(
        "fn probe(flag: bool) { while flag { break; } }",
        [](BodyConstruction& construction, const SemIRProgram&) static noexcept {
            const auto regions = ConstructionTestingFixture::regions(construction);
            for (const auto& region : regions) {
                for (const auto& statement : region.statements) {
                    const auto* loop = std::get_if<ConstructionLoop>(&statement.value);
                    if (loop == nullptr) {
                        continue;
                    }
                    auto& body = regions[loop->body.index()];
                    if (body.statements.empty()) {
                        continue;
                    }
                    regions[loop->steps.index()].statements.push_back(body.statements.front());
                    body.statements.erase(body.statements.begin());
                    return true;
                }
            }
            return false;
        },
        ConstructionViolationKind::InvalidControl
    );
}

TEST_CASE("Construction: nested loops handlers and closure bodies pass their publication check") {
    const auto semantic = analyze_test_program(
        "struct Error {} fn fail() throw Error { throw Error {}; } "
        "fn probe(flag: bool) throw Error { let closure = [flag]() { while flag { break; } }; "
        "while flag { try { while flag { fail()?; break; } } catch { Error(_) => { "
        "try { fail()?; } catch { Error(_) => rethrow, } }, } break; } closure(); }"
    );
    auto count = 0uz;
    for (const auto entry : semantic.bodies().entries()) {
        const auto construction = construct_body(semantic, entry.id);
        CHECK(validate_construction(construction, entry.value).has_value());
        ++count;
    }
    CHECK(count >= 3uz);
}

TEST_CASE("Construction: loop transfers retain their explicit enclosing destination") {
    const auto semantic = analyze_test_program("fn probe(flag: bool) { while flag { break; } }\n");
    auto checked = false;
    for (const auto entry : semantic.bodies().entries()) {
        const auto construction = construct_body(semantic, entry.id);
        for (const auto& statement : construction.region(construction.root()).statements) {
            const auto* scope = std::get_if<ConstructionScope>(&statement.value);
            if (scope == nullptr) {
                continue;
            }
            const auto& loop_region = construction.region(scope->region);
            REQUIRE(loop_region.statements.size() == 1uz);
            const auto* loop = std::get_if<ConstructionLoop>(&loop_region.statements.front().value);
            REQUIRE(loop != nullptr);
            const auto& loop_body = construction.region(loop->body);
            REQUIRE_FALSE(loop_body.statements.empty());
            const auto* transfer =
                std::get_if<ConstructionLoopTransfer>(&loop_body.statements.front().value);
            REQUIRE(transfer != nullptr);
            CHECK(transfer->loop == scope->region);
            CHECK_FALSE(transfer->continue_loop);
            REQUIRE(loop->condition.has_value());
            CHECK(
                construction.expression(*loop->condition).lifetime.owner()
                == entry.value.region().lifetime.owner()
            );
            checked = true;
        }
    }
    CHECK(checked);
}

TEST_CASE("Construction: discard distinguishes an operation from its effectful operands") {
    const auto semantic = analyze_test_program(
        "fn touch(&n: i32) -> i32 { n += 1; return n; }\nfn probe(&n: i32) { let _ = touch(&n) + 1; let _ = 1 / n; }\n"
    );
    auto checked_add = false;
    auto checked_divide = false;
    for (const auto entry : semantic.bodies().entries()) {
        const auto construction = construct_body(semantic, entry.id);
        for (const auto& statement : construction.region(construction.root()).statements) {
            auto input = std::optional<ConstructionExpressionID>();
            if (const auto* discard = std::get_if<ConstructionDiscard>(&statement.value)) {
                input = discard->expression;
            }
            if (const auto* initialization =
                    std::get_if<ConstructionInitialize>(&statement.value)) {
                input = initialization->initializer;
            }
            if (!input) {
                continue;
            }
            const auto& expression = construction.expression(*input);
            const auto* binary = std::get_if<SemBinary>(&expression.operation.value);
            const auto inputs = construction_operands(expression);
            if (binary == nullptr) {
                continue;
            }
            if (binary->operation == BinaryOperator::Add) {
                CHECK_FALSE(expression.executes_operation);
                CHECK(expression.requires_execution);
                CHECK(construction.expression(inputs[0].expression).executes_operation);
                checked_add = true;
            }
            if (binary->operation == BinaryOperator::Divide) {
                CHECK(expression.executes_operation);
                checked_divide = true;
            }
        }
    }
    CHECK(checked_add);
    CHECK(checked_divide);
}

TEST_CASE("Construction: native Take retains its distinct C++ category contract") {
    const auto semantic = analyze_test_program(
        "fn consume(&&value: i32) {}\n"
        "fn probe() { var first = 1; var second = 2; consume(&&first); ::native_take(&&second); }\n"
    );
    auto checked_native = false;
    auto checked_carven = false;
    for (const auto entry : semantic.bodies().entries()) {
        const auto construction = construct_body(semantic, entry.id);
        for (const auto& expression : construction.expression_values()) {
            const auto inputs = construction_operands(expression);
            if (std::holds_alternative<SemCppCall>(expression.operation.value)) {
                REQUIRE(inputs.size() == 1uz);
                CHECK(inputs.front().use == ConstructionUse::NativeTake);
                CHECK(
                    std::holds_alternative<SemTake>(
                        construction.expression(inputs.front().expression).operation.value
                    )
                );
                checked_native = true;
            }
            if (std::holds_alternative<SemCall>(expression.operation.value)) {
                REQUIRE(inputs.size() == 2uz);
                CHECK(inputs.back().use == ConstructionUse::Consume);
                checked_carven = true;
            }
        }
    }
    CHECK(checked_native);
    CHECK(checked_carven);
}
