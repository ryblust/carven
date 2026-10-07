module carven:test.internal.frontend.parse.control;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.ast.type;
import :frontend.literal;
import :test.harness.framework;
import :test.internal.frontend.parse.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Parser control: ordinary and branch blocks preserve different result rules"_test =
        [] static noexcept {
            const auto result = parse_valid(
                "fn choose(ready) {\n"
                " if ready { work(); } else { fallback(); }\n"
                " let value = if ready { work(); 1 } else { 0 };\n"
                "}\n"
            );
            const auto ast = result.view();
            const auto& body = function_body(result);
            if (!expect_equal(body.statements.size(), 2uz)) {
                return;
            }

            const auto& statement_if = get<ASTIfForm>(ast.statement(body.statements[0]));
            expect(!ast.branch_block(statement_if.branches[0].body).result.has_value());
            if (!expect(statement_if.else_branch.has_value())) {
                return;
            }
            expect(!ast.branch_block(*statement_if.else_branch).result.has_value());

            const auto& declaration = get<ASTVariableDecl>(ast.statement(body.statements[1]));
            const auto& value_if = get<ASTIfForm>(ast.expression(*declaration.initializer));
            expect(ast.branch_block(value_if.branches[0].body).result.has_value());
            if (!expect(value_if.else_branch.has_value())) {
                return;
            }
            expect(ast.branch_block(*value_if.else_branch).result.has_value());
        };

    "Parser control: const marks static if chains and range loops"_test = [] static noexcept {
        static constexpr auto text = std::string_view(
            "fn unroll(const count: usize) {\n"
            " const for index in 0..count { const if index == 0 { continue; } else if index > 2 {} }\n"
            " for index in 0..count {}\n"
            " let value = const if count == 0 { 1 } else { 2 };\n"
            " const limit = count;\n"
            "}\n"
        );
        const auto result = parse_valid(text);
        const auto ast = result.view();
        const auto& body = function_body(result);
        if (!expect_equal(body.statements.size(), 4uz)) {
            return;
        }

        const auto& unrolled = get<ASTForStmt>(ast.statement(body.statements[0]));
        if (!expect(unrolled.const_span.has_value())) {
            return;
        }
        expect_equal(slice(text, *unrolled.const_span), std::string_view("const"));
        expect_equal(
            slice(text, ast.statement(body.statements[0]).span).substr(0, 9),
            std::string_view("const for")
        );
        const auto& inner = ast.block(unrolled.body);
        const auto& selected = get<ASTIfForm>(ast.statement(inner.statements[0]));
        expect(selected.const_span.has_value());
        expect_equal(selected.branches.size(), 2uz);
        expect_equal(slice(text, selected.span).substr(0, 8), std::string_view("const if"));

        const auto& runtime = get<ASTForStmt>(ast.statement(body.statements[1]));
        expect(!runtime.const_span.has_value());

        const auto& declaration = get<ASTVariableDecl>(ast.statement(body.statements[2]));
        const auto& value_if = get<ASTIfForm>(ast.expression(*declaration.initializer));
        expect(value_if.const_span.has_value());

        expect_equal(
            get<ASTVariableDecl>(ast.statement(body.statements[3])).kind,
            ASTBindingKind::Const
        );

        check_invalid(
            "fn f() { const for var i = 0; i < 2; i += 1 {} }",
            "'const for' requires a range header"
        );
        check_invalid("fn f() { const for ; true; {} }", "'const for' requires a range header");
    };

    "Parser control: patterns own inline type children and typed match-arm bodies"_test =
        [] static noexcept {
            const auto result = parse_valid(
                "fn classify(value) {\n"
                " match value {\n"
                "  _ => fallback(),\n"
                "  -1 | 0 | State::Ready if ready => { prepare(); result },\n"
                "  is Number => return 1,\n"
                "  is [i32; 4] => continue,\n"
                " }\n"
                "}\n"
            );
            const auto ast = result.view();
            const auto& body = function_body(result);
            const auto& match = get<ASTMatchForm>(ast.statement(body.statements[0]));
            if (!expect_equal(match.arms.size(), 4uz)) {
                return;
            }

            expect(is<ASTWildcardPattern>(ast.pattern(match.arms[0].pattern)));
            expect(is<ASTExprID>(match.arms[0].body));

            const auto& alternatives = get<ASTOrPattern>(ast.pattern(match.arms[1].pattern));
            if (!expect_equal(alternatives.alternatives.size(), 3uz)) {
                return;
            }
            const auto& negative =
                get<ASTNegativeNumberPattern>(ast.pattern(alternatives.alternatives[0]));
            expect_equal(std::get<IntegerLiteralValue>(negative.value).magnitude, 1u);
            expect(is<ASTLiteral>(ast.pattern(alternatives.alternatives[1])));
            expect(is<ASTCasePattern>(ast.pattern(alternatives.alternatives[2])));
            expect(match.arms[1].guard.has_value());
            expect(is<ASTNameExpr>(ast.expression(match.arms[1].guard->expression)));
            expect(is<ASTBranchBlockID>(match.arms[1].body));

            const auto& number_constraint =
                get<ASTConstraintPattern>(ast.pattern(match.arms[2].pattern));
            expect(is<ASTQualifiedName>(number_constraint.operand));
            expect(is<ASTControlTransfer>(match.arms[2].body));

            const auto& array_constraint =
                get<ASTConstraintPattern>(ast.pattern(match.arms[3].pattern));
            expect(is<ASTArrayType>(array_constraint.operand));
            if (!expect_equal(ast.types().size(), 1uz)) {
                return;
            }
            expect(is<ASTNamedType>(ast.types().front()));

            parse_valid("fn f() { match value { _ | 1 => value() } }");
            check_invalid("fn f() { match value { is 1 => value() } }");
            check_invalid("fn f() { match value { 0 => return value; } }");
            check_invalid(
                "fn f() { match value { -State => value } }",
                "expected number after '-' in pattern"
            );
        };

    "Parser control: payload binding markers preserve explicit access"_test = [] static noexcept {
        const auto result = parse_valid(
            "fn inspect(value) { match value {"
            " .Triple(copy, ref read, &write) => {},"
            " .Nested(.Some(ref nested)) => {},"
            " .One(ref) => {},"
            " } }"
        );
        const auto ast = result.view();
        const auto& match = get<ASTMatchForm>(ast.statement(function_body(result).statements[0]));
        const auto& triple = get<ASTCasePattern>(ast.pattern(match.arms[0].pattern));
        require(triple.payload.has_value());
        const auto modes = std::array {
            ASTPatternBindingMode::Value,
            ASTPatternBindingMode::Read,
            ASTPatternBindingMode::Write,
        };
        for (auto index = 0uz; index < modes.size(); ++index) {
            const auto& binding =
                get<ASTBindingPattern>(ast.pattern(triple.payload->patterns[index]));
            expect_equal(binding.mode, modes[index]);
            expect_equal(binding.marker_span.has_value(), index != 0uz);
        }
        const auto& nested = get<ASTCasePattern>(ast.pattern(match.arms[1].pattern));
        require(nested.payload.has_value());
        const auto& some = get<ASTCasePattern>(ast.pattern(nested.payload->patterns[0]));
        require(some.payload.has_value());
        expect_equal(
            get<ASTBindingPattern>(ast.pattern(some.payload->patterns[0])).mode,
            ASTPatternBindingMode::Read
        );
        const auto& one = get<ASTCasePattern>(ast.pattern(match.arms[2].pattern));
        require(one.payload.has_value());
        expect_equal(
            get<ASTBindingPattern>(ast.pattern(one.payload->patterns[0])).mode,
            ASTPatternBindingMode::Value
        );
        check_invalid(
            "fn f() { match value { ref read => {} } }",
            "borrowed bindings require an enum case payload"
        );
        check_invalid(
            "fn f() { match value { &write => {} } }",
            "borrowed bindings require an enum case payload"
        );
        check_invalid(
            "fn f() { match value { .One(&1) => {} } }",
            "expected borrowed payload binding name"
        );
        check_invalid("fn f() { match value { .One(&&take) => {} } }");
    };

    "Parser control: enum case patterns recursively own positional patterns"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "fn inspect(shape) { match shape {"
                " .Circle(radius) if radius > 0.0 => radius,"
                " Shape::Rect(_, .Some(value) | .None) => value,"
                " .Point => 0.0,"
                " } }"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            const auto& body = function_body(result);
            const auto& match = get<ASTMatchForm>(ast.statement(body.statements[0]));
            if (!expect_equal(match.arms.size(), 3uz)) {
                return;
            }

            const auto& circle = get<ASTCasePattern>(ast.pattern(match.arms[0].pattern));
            expect(std::holds_alternative<ASTContextualCaseQualifier>(circle.qualifier));
            if (!expect(circle.payload.has_value())) {
                return;
            }
            if (!expect_equal(circle.payload->patterns.size(), 1uz)) {
                return;
            }
            expect(is<ASTBindingPattern>(ast.pattern(circle.payload->patterns[0])));
            if (!expect(match.arms[0].guard.has_value())) {
                return;
            }
            expect(is<ASTBinaryExpr>(ast.expression(match.arms[0].guard->expression)));

            const auto& rectangle = get<ASTCasePattern>(ast.pattern(match.arms[1].pattern));
            if (!expect(std::holds_alternative<ASTQualifiedCaseQualifier>(rectangle.qualifier))) {
                return;
            }
            expect_equal(
                std::get<ASTQualifiedCaseQualifier>(rectangle.qualifier).components.size(),
                1uz
            );
            if (!expect(rectangle.payload.has_value())) {
                return;
            }
            if (!expect_equal(rectangle.payload->patterns.size(), 2uz)) {
                return;
            }
            expect(is<ASTWildcardPattern>(ast.pattern(rectangle.payload->patterns[0])));
            const auto& nested_or = get<ASTOrPattern>(ast.pattern(rectangle.payload->patterns[1]));
            if (!expect_equal(nested_or.alternatives.size(), 2uz)) {
                return;
            }
            expect(is<ASTCasePattern>(ast.pattern(nested_or.alternatives[0])));
            expect(is<ASTCasePattern>(ast.pattern(nested_or.alternatives[1])));

            const auto& point = get<ASTCasePattern>(ast.pattern(match.arms[2].pattern));
            expect(!point.payload.has_value());

            check_invalid("fn f(x) { match x { .Case(value => value } }");
        };

    "Parser control: semantic invalidity does not reject a grammar AST"_test = [] static noexcept {
        static constexpr auto accepted = std::to_array<std::string_view>({
            "fn main(args: i32, extra) -> Text { break; }",
            "fn f() { return; continue; }",
            "fn f() { let value = if ready { 1 }; }",
            "fn f() { let value = match subject {}; }",
            "fn f() { match subject { _ => first(), 0 => unreachable() } }",
        });
        each(accepted, std::identity {}, [](std::string_view text) static noexcept {
            parse_valid(text);
        });
    };

    "Parser control: outer parentheses do not change nested block boundaries"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "fn nested(ready) => (if ready { (if ready { (if ready { 1 } else { 2 }) }"
                " else { 3 }) } else { 4 });"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            require_equal(root(result).items.size(), 1uz);
            const auto& implementation = get<ASTFunctionBody>(function(result).implementation);
            require(is<ASTExpressionBody>(implementation.body));
            auto expression = get<ASTExpressionBody>(implementation.body).expression;
            for (auto level = 0uz; level < 3uz; ++level) {
                require(is<ASTGroupExpr>(ast.expression(expression)));
                expression = get<ASTGroupExpr>(ast.expression(expression)).expression;
                require(is<ASTIfForm>(ast.expression(expression)));
                const auto& conditional = get<ASTIfForm>(ast.expression(expression));
                require_equal(conditional.branches.size(), 1uz);
                expect_equal(
                    slice(text, ast.expression(conditional.branches[0].condition).span),
                    std::string_view("ready")
                );
                const auto& branch = ast.branch_block(conditional.branches[0].body);
                expect(branch.statements.empty());
                require(branch.result.has_value());
                require(conditional.else_branch.has_value());
                const auto& fallback = ast.branch_block(*conditional.else_branch);
                expect(fallback.statements.empty());
                require(fallback.result.has_value());
                expect_equal(
                    slice(text, ast.expression(*fallback.result).span),
                    std::to_string(4uz - level)
                );
                expression = *branch.result;
            }
            expect_equal(slice(text, ast.expression(expression).span), std::string_view("1"));
            check_invalid("fn nested(ready) => (if ready { (if ready { 1 } else 2) } else { 3 });");
            static_cast<void>(
                parse_valid("fn nested() => (if (Flag { true }).value { 1 } else { 2 });")
            );
        };
});

} // namespace
