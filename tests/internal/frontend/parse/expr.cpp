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
import :support.unique_indirect;
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
    "Parser expression: tuple commas distinguish values from groups"_test = [] static noexcept {
        const auto tree = parse_valid(
            "fn f() { let unit = (); let grouped = (1); let single = (1,); let nested = (1, (2, ()),); }"
        );
        const auto ast = tree.view();
        expect(get<ASTTupleExpr>(initializer(tree, 0)).elements.empty());
        expect(is<ASTGroupExpr>(initializer(tree, 1)));
        expect_equal(get<ASTTupleExpr>(initializer(tree, 2)).elements.size(), 1uz);
        const auto& outer = get<ASTTupleExpr>(initializer(tree, 3));
        if (!expect_equal(outer.elements.size(), 2uz)) {
            return;
        }
        const auto& inner = get<ASTTupleExpr>(ast.expression(outer.elements[1]));
        if (!expect_equal(inner.elements.size(), 2uz)) {
            return;
        }
        expect(get<ASTTupleExpr>(ast.expression(inner.elements[1])).elements.empty());
        check_invalid("fn f() { let value = (,); }");
        check_invalid("fn f() { let value = (1,,2); }");
        check_invalid("fn f() { let value = (1,2; }");
    };

    "Parser expression: tuple positions are checked decimal components and chain naturally"_test =
        [] static noexcept {
            constexpr auto text = std::string_view(
                "fn f() { pair.0.1; pair.4294967295; 1.0; pair.0.member[2]?; pointer->0.1; }"
            );
            const auto tree = parse_valid(text);
            const auto ast = tree.view();
            const auto& outer = get<ASTTupleElementExpr>(expression_statement(tree, 0uz));
            expect_equal(outer.index, 1u);
            expect_equal(slice(text, outer.index_span), std::string_view("1"));
            const auto& inner = get<ASTTupleElementExpr>(ast.expression(outer.operand_id));
            expect_equal(inner.index, 0u);
            expect(is<ASTNameExpr>(ast.expression(inner.operand_id)));
            expect_equal(
                get<ASTTupleElementExpr>(expression_statement(tree, 1uz)).index,
                std::numeric_limits<std::uint32_t>::max()
            );
            expect(
                std::holds_alternative<FloatingLiteralValue>(
                    get<ASTLiteral>(expression_statement(tree, 2uz)).value
                )
            );
            const auto& propagated = get<ASTPropagationExpr>(expression_statement(tree, 3uz));
            const auto& indexed = get<ASTIndexExpr>(ast.expression(propagated.operand_id));
            const auto& member = get<ASTMemberExpr>(ast.expression(indexed.operand_id));
            expect(is<ASTTupleElementExpr>(ast.expression(member.operand_id)));
            const auto& arrow_outer = get<ASTTupleElementExpr>(expression_statement(tree, 4uz));
            expect_equal(arrow_outer.index, 1u);
            const auto& arrow_inner =
                get<ASTTupleElementExpr>(ast.expression(arrow_outer.operand_id));
            expect_equal(arrow_inner.index, 0u);
            const auto& dereference = get<ASTPrefixExpr>(ast.expression(arrow_inner.operand_id));
            expect_equal(dereference.op, ASTPrefixOperator::Dereference);
            expect_equal(slice(text, dereference.operator_span), std::string_view("->"));
            expect(is<ASTNameExpr>(ast.expression(dereference.operand_id)));
            for (const auto index :
                 {"00",
                  "01",
                  "1u32",
                  "1f32",
                  "1e2",
                  "0x1",
                  "0b1",
                  "-1",
                  "4294967296",
                  "999999999999999999999999999"}) {
                check_invalid(std::format("fn f() {{ pair.{}; }}", index));
                check_invalid(std::format("fn f() {{ pair->{}; }}", index));
            }
            check_invalid("fn f() { pair::0; }");
            check_invalid("fn f() { .0; }");
        };

    "Parser expression: lambda tuple parameters retain recursive targets and access"_test =
        [] static noexcept {
            const auto tree = parse_valid(
                "fn f() { let callback = []((a, (_, b)): (i32, ((), bool)), &(c,): (i32,), &&(): ()) => a; }"
            );
            const auto& lambda = get<ASTLambdaExpr>(initializer(tree, 0uz));
            if (!expect_equal(lambda.parameters.size(), 3uz)) {
                return;
            }
            const auto& target =
                *get<UniqueIndirect<ASTTupleBindingTarget>>(lambda.parameters[0].target);
            if (!expect_equal(target.elements.size(), 2uz)) {
                return;
            }
            const auto& nested = *get<UniqueIndirect<ASTTupleBindingTarget>>(target.elements[1]);
            if (!expect_equal(nested.elements.size(), 2uz)) {
                return;
            }
            expect(is<ASTDiscardBindingTarget>(nested.elements[0]));
            expect_equal(lambda.parameters[1].access.mode, ASTAccessMode::Write);
            expect_equal(lambda.parameters[2].access.mode, ASTAccessMode::Take);
            expect((*get<UniqueIndirect<ASTTupleBindingTarget>>(lambda.parameters[2].target))
                       .elements.empty());
            check_invalid("fn f() { let callback = []((a, 1)) => a; }");
        };

    "Parser async: builtin calls use ordinary names while native roots remain explicit"_test =
        [] static noexcept {
            constexpr auto text =
                std::string_view("fn f() { cancel_requested(); ::async::when_all(); }");
            const auto tree = parse_valid(text);
            const auto ast = tree.view();
            const auto& call = get<ASTCallExpr>(expression_statement(tree, 0uz));
            const auto& name = get<ASTNameExpr>(ast.expression(call.callee));
            expect_equal(slice(text, name.name_span), std::string_view("cancel_requested"));
            const auto& native_call = get<ASTCallExpr>(expression_statement(tree, 1uz));
            expect(is<ASTCppNameExpr>(ast.expression(native_call.callee)));
            check_invalid("fn f() { async::when_all(); }");
            check_invalid("fn f() { let value = (async)::when_all(); }");
            parse_valid(
                "fn f() { let value = if true { cancel_requested() } else { false }; for cancel_requested(); false; {} }"
            );
        };

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

    "Parser expression: qualified and member names retain keyword spellings"_test =
        [] static noexcept {
            constexpr auto text = std::string_view(
                "fn names() { ::async::await(); value.cancel(); value->match; "
                "vendor::test(); let value = ::async::match { 7 }; }"
            );
            const auto tree = parse_valid(text);
            const auto ast = tree.view();
            const auto& global_call = get<ASTCallExpr>(expression_statement(tree, 0uz));
            const auto& global = get<ASTCppNameExpr>(ast.expression(global_call.callee));
            if (!expect_equal(global.components.size(), 2uz)) {
                return;
            }
            expect_equal(slice(text, global.components[0]), std::string_view("async"));
            expect_equal(slice(text, global.components[1]), std::string_view("await"));
            const auto& member_call = get<ASTCallExpr>(expression_statement(tree, 1uz));
            const auto& member = get<ASTMemberExpr>(ast.expression(member_call.callee));
            expect_equal(slice(text, member.name_span), std::string_view("cancel"));
            const auto& arrow = get<ASTMemberExpr>(expression_statement(tree, 2uz));
            expect_equal(slice(text, arrow.name_span), std::string_view("match"));
            expect(is<ASTPrefixExpr>(ast.expression(arrow.operand_id)));
            const auto& scope_call = get<ASTCallExpr>(expression_statement(tree, 3uz));
            const auto& scope = get<ASTMemberExpr>(ast.expression(scope_call.callee));
            expect_equal(scope.op, ASTMemberOperator::Scope);
            expect_equal(slice(text, scope.name_span), std::string_view("test"));
            const auto& constructed = get<ASTConstructionExpr>(initializer(tree, 4uz));
            expect(is<ASTNamedType>(*constructed.type));
            check_invalid("fn invalid() { let await = 7; }");
            check_invalid("fn invalid() { ::vendor::7(); }");
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

    "Parser expression: array initialization retains one element and its extent"_test =
        [] static noexcept {
            const auto result = parse_valid("fn arrays() { let values = [make(); 3]; }");
            const auto ast = result.view();
            const auto& repeated = get<ASTArrayRepeatExpr>(initializer(result, 0));
            expect(is<ASTCallExpr>(ast.expression(repeated.element)));
            expect(is<ASTLiteral>(ast.expression(repeated.extent)));
            check_invalid("fn bad() { let values = [1;]; }");
            check_invalid("fn bad() { let values = [1, 2; 3]; }");
            check_invalid("fn bad() { let values = [1; 3, 4]; }");
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
            const auto& access = get<ASTAccessExpr>(ast.expression(call.arguments.front()));
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
            [](const char* source) static noexcept { check_invalid(source); }
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
            check_invalid("fn f() { let value = Pair { ..., first: 1 }; }");
            check_invalid("fn f() { let value = Pair { first: 1, ..., ... }; }");
            check_invalid("fn f() { let value = Pair { 1, ... }; }");
        };

    "Parser expression: type applications preserve callee and associated type arguments"_test =
        [] static noexcept {
            constexpr auto text = std::string_view(
                "fn use() { "
                "relay<Box<i32,>,>(value); "
                "Owner<Box<i32>>::new(value); "
                "let box = Box<i32,> { value: 1 }; }"
            );
            const auto tree = parse_valid(text);
            const auto ast = tree.view();
            const auto& direct = get<ASTCallExpr>(expression_statement(tree, 0));
            const auto& application = get<ASTTypeApplicationExpr>(ast.expression(direct.callee));
            if (!expect_equal(application.arguments.size(), 1uz)) {
                return;
            }
            expect(is<ASTNameExpr>(ast.expression(application.operand_id)));
            expect_equal(slice(text, application.arguments_span), std::string_view("<Box<i32,>,>"));
            const auto& nested =
                get<ASTNamedType>(ast.type(std::get<ASTTypeID>(application.arguments[0].value)));
            expect_equal(nested.arguments.size(), 1uz);
            const auto& associated = get<ASTCallExpr>(expression_statement(tree, 1));
            const auto& member = get<ASTMemberExpr>(ast.expression(associated.callee));
            expect_equal(member.op, ASTMemberOperator::Scope);
            expect(is<ASTTypeApplicationExpr>(ast.expression(member.operand_id)));
            const auto& construction = get<ASTConstructionExpr>(initializer(tree, 2));
            if (!expect(construction.type.has_value())) {
                return;
            }
            const auto& constructed_type = get<ASTNamedType>(*construction.type);
            expect_equal(constructed_type.arguments.size(), 1uz);
        };

    "Parser expression: complete type application selects syntax without name lookup"_test =
        [] static noexcept {
            const auto tree =
                parse_valid("fn use() { a < b > (c); (a < b) > c; a < b; value >> amount; }");
            const auto& call = get<ASTCallExpr>(expression_statement(tree, 0));
            expect(is<ASTTypeApplicationExpr>(tree.view().expression(call.callee)));
            const auto& comparison = get<ASTBinaryExpr>(expression_statement(tree, 1));
            expect_equal(comparison.op, ASTBinaryOperator::Greater);
            const auto& less = get<ASTBinaryExpr>(expression_statement(tree, 2));
            expect_equal(less.op, ASTBinaryOperator::Less);
            const auto& shift = get<ASTBinaryExpr>(expression_statement(tree, 3));
            expect_equal(shift.op, ASTBinaryOperator::RightShift);
            check_invalid("fn use() { relay<>(value); }");
            check_invalid("fn use() { relay<i32,,u8>(value); }");
            check_invalid("fn use() { relay<4>(value); }");
        };
});

} // namespace
