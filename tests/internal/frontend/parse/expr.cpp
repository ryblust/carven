module carven:test.internal.frontend.parse.expr;

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
import :source.text;
import :test.harness.framework;
import :test.internal.frontend.parse.fixture;
import std;

namespace {

auto body_of(const SyntaxTree& tree) noexcept -> const ASTBlock& {
    return function_body(tree);
}

auto expression_statement(const SyntaxTree& tree, std::size_t index) noexcept -> const ASTExpr& {
    const auto ast = tree.view();
    const auto& statement = ast.statement(body_of(tree).statements[index]);
    return ast.expression(get<ASTExprStatement>(statement).expression);
}

auto initializer(const SyntaxTree& tree, std::size_t index) noexcept -> const ASTExpr& {
    const auto ast = tree.view();
    const auto& declaration = get<ASTVariableDecl>(ast.statement(body_of(tree).statements[index]));
    return ast.expression(*declaration.initializer);
}

const TestSuite suite([] static noexcept {
    "Parser async: propagation wraps await and preserves grouped inner propagation"_test =
        [] static noexcept {
            const auto tree = parse_valid("async fn f() { await operation?; await (operation?); }");
            const auto& propagated = get<ASTPropagationExpr>(expression_statement(tree, 0uz));
            const auto& awaited = get<ASTPrefixExpr>(tree.view().expression(propagated.operand_id));
            expect_equal(awaited.op, ASTPrefixOperator::Await);
            expect(is<ASTNameExpr>(tree.view().expression(awaited.operand_id)));
            const auto& grouped = get<ASTPrefixExpr>(expression_statement(tree, 1uz));
            expect_equal(grouped.op, ASTPrefixOperator::Await);
            const auto& group = get<ASTGroupExpr>(tree.view().expression(grouped.operand_id));
            expect(is<ASTPropagationExpr>(tree.view().expression(group.expression)));
        };

    "Parser expression: literals retain typed lexer values"_test = [] static noexcept {
        static constexpr auto text = std::string_view(
            "fn values() { 1; 1f32; 1.0; 0xffu8; 0b10; 0o7; 'x'; \"text\"; true; false; }"
        );
        const auto result = parse_valid(text);
        const auto literal_at = [&](std::size_t index) noexcept -> const ASTLiteral& {
            return get<ASTLiteral>(expression_statement(result, index));
        };
        expect_equal(std::get<IntegerLiteralValue>(literal_at(0).value).base, IntegerBase::Decimal);
        expect_equal(
            std::get<FloatingLiteralValue>(literal_at(1).value).suffix,
            NumericSuffix::F32
        );
        expect_equal(
            slice(text, std::get<FloatingLiteralValue>(literal_at(1).value).value_span),
            std::string_view("1")
        );
        expect_equal(
            std::get<IntegerLiteralValue>(literal_at(3).value).base,
            IntegerBase::Hexadecimal
        );
        expect_equal(std::get<IntegerLiteralValue>(literal_at(4).value).base, IntegerBase::Binary);
        expect_equal(std::get<IntegerLiteralValue>(literal_at(5).value).base, IntegerBase::Octal);
        expect(((std::get<CharacterLiteralValue>(literal_at(6).value).scalar) == (U'x')))
            .note("std::get<CharacterLiteralValue>(literal_at(6).value).scalar == U'x'");
        expect_equal(
            std::get<StringLiteralValue>(literal_at(7).value).bytes,
            std::string_view("text")
        );
        expect(std::get<BooleanLiteralValue>(literal_at(8).value).value);
        expect(!std::get<BooleanLiteralValue>(literal_at(9).value).value);
    };

    "Parser expression: primary and postfix forms keep construction types inline"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "fn expressions() {"
                " let empty = []; let grouped = (value); let defaulted = Box {};"
                " let positional = Pair { 1, 2, }; let fields = Point { x: 1, y: 2, };"
                " factory(1, 2,)[index].field::member;"
                " let callback = fn(i32) -> bool { predicate };"
                "}"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            expect(get<ASTArrayExpr>(initializer(result, 0)).element_ids.empty());
            expect(is<ASTGroupExpr>(initializer(result, 1)));

            const auto& defaulted = get<ASTConstructionExpr>(initializer(result, 2));
            expect(is<ASTNamedType>(*defaulted.type));
            expect(is<std::monostate>(defaulted.initializer));

            const auto& positional = get<ASTConstructionExpr>(initializer(result, 3));
            expect_equal(
                get<ASTPositionalInitializerList>(positional.initializer).values.size(),
                2uz
            );
            const auto& fields = get<ASTConstructionExpr>(initializer(result, 4));
            expect_equal(get<ASTFieldInitializerList>(fields.initializer).fields.size(), 2uz);

            const auto& final_member = get<ASTMemberExpr>(expression_statement(result, 5));
            expect_equal(slice(text, final_member.name_span), std::string_view("member"));
            const auto& field = get<ASTMemberExpr>(ast.expression(final_member.operand_id));
            const auto& index = get<ASTIndexExpr>(ast.expression(field.operand_id));
            const auto& call = get<ASTCallExpr>(ast.expression(index.operand_id));
            expect_equal(call.arguments.size(), 2uz);

            expect(is<ASTFunctionType>(*get<ASTConstructionExpr>(initializer(result, 6)).type));
            if (!expect_equal(ast.types().size(), 2uz)) {
                return;
            }
            expect(std::ranges::all_of(ast.types(), [](const ASTType& type) static noexcept {
                return is<ASTNamedType>(type);
            }));

            check_invalid("fn invalid() { let native = #[cpp] ---\nreturn compute();\n---\n; }");
        };

    "Parser expression: precedence is represented by typed expression edges"_test =
        [] static noexcept {
            const auto result = parse_valid(
                "fn expression() { result = !-value + call(1, 2,)[index].field * 3 "
                "<< 1 == limit && ready || done; }"
            );
            const auto ast = result.view();
            const auto& assignment =
                get<ASTAssignment>(ast.statement(body_of(result).statements[0]));
            const auto& logical_or = get<ASTBinaryExpr>(ast.expression(assignment.value));
            expect_equal(logical_or.op, ASTBinaryOperator::LogicalOr);
            expect(is<ASTNameExpr>(ast.expression(logical_or.right)));
            const auto& logical_and = get<ASTBinaryExpr>(ast.expression(logical_or.left));
            expect_equal(logical_and.op, ASTBinaryOperator::LogicalAnd);
            const auto& comparison = get<ASTBinaryExpr>(ast.expression(logical_and.left));
            expect_equal(comparison.op, ASTBinaryOperator::Equal);

            check_invalid("fn expression() { a < b < c; }");
            check_invalid("fn expression() { (a, b); }");
        };

    "Parser expression: as is left associative between prefix and multiplicative expressions"_test =
        [] static noexcept {
            const auto result = parse_valid(
                "fn expression() { let value = -input as i32 as i64 * 2 + 1; update(&input); }"
            );
            const auto ast = result.view();
            const auto& additive = get<ASTBinaryExpr>(initializer(result, 0));
            const auto& multiply = get<ASTBinaryExpr>(ast.expression(additive.left));
            const auto& outer_cast = get<ASTCastExpr>(ast.expression(multiply.left));
            const auto& inner_cast = get<ASTCastExpr>(ast.expression(outer_cast.operand_id));
            expect(is<ASTPrefixExpr>(ast.expression(inner_cast.operand_id)));

            const auto& call = get<ASTCallExpr>(expression_statement(result, 1));
            if (!expect_equal(call.arguments.size(), 1uz)) {
                return;
            }
            const auto& access =
                get<ASTAccessExpr>(ast.expression(call.arguments.front().expression));
            expect_equal(access.mode, ASTAccessMode::Write);
            expect(is<ASTNameExpr>(ast.expression(access.operand_id)));
        };

    "Parser expression: access expressions cover the following complete expression"_test =
        [] static noexcept {
            const auto result = parse_valid(
                "fn access() { let taken = &&left + right; let updated = &(target); "
                "let logical = left && right; let bitwise = left & right; }"
            );
            const auto ast = result.view();

            const auto& take = get<ASTAccessExpr>(initializer(result, 0));
            expect_equal(take.mode, ASTAccessMode::Take);
            expect(is<ASTBinaryExpr>(ast.expression(take.operand_id)));

            const auto& write = get<ASTAccessExpr>(initializer(result, 1));
            expect_equal(write.mode, ASTAccessMode::Write);
            expect(is<ASTGroupExpr>(ast.expression(write.operand_id)));

            expect_equal(
                get<ASTBinaryExpr>(initializer(result, 2)).op,
                ASTBinaryOperator::LogicalAnd
            );
            expect_equal(
                get<ASTBinaryExpr>(initializer(result, 3)).op,
                ASTBinaryOperator::BitwiseAnd
            );
        };

    "Parser expression: prefix expressions preserve written nesting"_test = [] static noexcept {
        const auto result = parse_valid("fn prefix() { !-~value; }");
        const auto ast = result.view();
        const auto& logical_not = get<ASTPrefixExpr>(expression_statement(result, 0));
        expect_equal(logical_not.op, ASTPrefixOperator::LogicalNot);
        const auto& negate = get<ASTPrefixExpr>(ast.expression(logical_not.operand_id));
        expect_equal(negate.op, ASTPrefixOperator::Negate);
        const auto& complement = get<ASTPrefixExpr>(ast.expression(negate.operand_id));
        expect_equal(complement.op, ASTPrefixOperator::BitwiseNot);
        expect(is<ASTNameExpr>(ast.expression(complement.operand_id)));
    };

    "Parser expression: contextual and qualified enum cases are structural expressions"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "fn cases() { let point = .Point; let circle = .Circle(1.0); Shape::Circle; }"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();

            const auto& point = get<ASTContextualCaseExpr>(initializer(result, 0));
            expect_equal(slice(text, point.dot_span), std::string_view("."));
            expect_equal(slice(text, point.name_span), std::string_view("Point"));

            const auto& call = get<ASTCallExpr>(initializer(result, 1));
            expect(is<ASTContextualCaseExpr>(ast.expression(call.callee)));
            if (!expect_equal(call.arguments.size(), 1uz)) {
                return;
            }

            const auto& qualified = get<ASTMemberExpr>(expression_statement(result, 2));
            expect_equal(qualified.op, ASTMemberOperator::Scope);
            expect_equal(slice(text, qualified.name_span), std::string_view("Circle"));
            expect(is<ASTNameExpr>(ast.expression(qualified.operand_id)));

            check_invalid("fn f() { let value = .; }");
        };

    "Parser expression: global C++ paths preserve their root and components"_test =
        [] static noexcept {
            constexpr auto text = std::string_view(
                "fn f() { ::vendor::calculate(1); let point = ::Point { 1, 2 }; }"
            );
            const auto tree = parse_valid(text);
            const auto ast = tree.view();
            const auto& call = get<ASTCallExpr>(expression_statement(tree, 0uz));
            const auto& name = get<ASTCppNameExpr>(ast.expression(call.callee));
            expect_equal(slice(text, name.global_root), std::string_view("::"));
            if (!expect_equal(name.components.size(), 2uz)) {
                return;
            }
            expect_equal(slice(text, name.components[0]), std::string_view("vendor"));
            expect_equal(slice(text, name.components[1]), std::string_view("calculate"));
            expect_equal(
                slice(text, ast.expression(call.callee).span),
                std::string_view("::vendor::calculate")
            );
            const auto& construction = get<ASTConstructionExpr>(initializer(tree, 1uz));
            const auto& type = get<ASTNamedType>(*construction.type);
            if (!expect(type.global_root.has_value())) {
                return;
            }
            expect_equal(slice(text, *type.global_root), std::string_view("::"));
        };

    "Parser expression: incomplete global C++ paths are rejected"_test = [] static noexcept {
        constexpr auto cases = std::array {
            "fn f() { ::; }",
            "fn f() { ::vendor::(); }",
            "fn f(value: ::) {}",
            "fn f() { let x = :: {}; }",
            "fn f() { ::*(); }",
            "fn f() { ::foo::::bar(); }",
        };
        each(
            cases,
            [](const char* source) static noexcept -> std::string_view { return source; },
            [](const char* source) static noexcept { check_rejected(source); }
        );
    };

    "Parser expression: contextual construction keeps the absent type explicit"_test =
        [] static noexcept {
            const auto tree = parse_valid("fn f() { let value: Pair = { first: 1, second: {} }; }");
            const auto& construction = get<ASTConstructionExpr>(initializer(tree, 0uz));
            expect(!construction.type.has_value());
            const auto& fields =
                std::get<ASTFieldInitializerList>(construction.initializer.value).fields;
            if (!expect_equal(fields.size(), 2uz)) {
                return;
            }
            const auto& empty = get<ASTConstructionExpr>(tree.view().expression(fields[1].value));
            expect(!empty.type.has_value());
            expect(std::holds_alternative<std::monostate>(empty.initializer.value));
            check_invalid("fn f() { let value: Pair = { 1, 2 }; }");
        };
});

} // namespace
