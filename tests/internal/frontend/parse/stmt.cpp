module carven:test.internal.frontend.parse.stmt;

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
import :source.text;
import :support.unique_indirect;
import :test.harness.framework;
import :test.internal.frontend.parse.fixture;
import std;

namespace {

auto body_of(const SyntaxTree& tree) noexcept -> const ASTBlock& {
    return function_body(tree);
}

auto statement_at(const SyntaxTree& tree, std::size_t index) noexcept -> const ASTStmt& {
    return tree.view().statement(body_of(tree).statements[index]);
}

const TestSuite suite([] static noexcept {
    "Parser statement: tuple bindings retain nested targets and discards"_test = [] static noexcept {
        constexpr auto text = std::string_view(
            "fn f() { let (a, _, (b, c)): (i32, (), (bool, i32)) = source; var (single,) = (1,); const (left, right) = (2, 3); let (grouped) = 4; }"
        );
        const auto tree = parse_valid(text);
        const auto& declaration = get<ASTVariableDecl>(statement_at(tree, 0));
        const auto& outer = *get<UniqueIndirect<ASTTupleBindingTarget>>(declaration.target);
        if (!expect_equal(outer.elements.size(), 3uz)) {
            return;
        }
        expect_equal(
            slice(text, get<ASTNamedBindingTarget>(outer.elements[0]).name_span),
            std::string_view("a")
        );
        expect(is<ASTDiscardBindingTarget>(outer.elements[1]));
        const auto& inner = *get<UniqueIndirect<ASTTupleBindingTarget>>(outer.elements[2]);
        if (!expect_equal(inner.elements.size(), 2uz)) {
            return;
        }
        expect_equal(
            slice(text, get<ASTNamedBindingTarget>(inner.elements[1]).name_span),
            std::string_view("c")
        );
        expect(is<ASTTupleType>(tree.view().type(*declaration.type)));
        expect_equal(get<ASTVariableDecl>(statement_at(tree, 1)).kind, ASTBindingKind::Var);
        expect_equal(get<ASTVariableDecl>(statement_at(tree, 2)).kind, ASTBindingKind::Const);
        expect(is<ASTNamedBindingTarget>(get<ASTVariableDecl>(statement_at(tree, 3)).target));
        check_invalid("fn f() { let (a,,b) = value; }");
        check_invalid("fn f() { let (a, 7) = value; }");
        check_invalid("fn f() { let (a, await) = value; }");
        check_invalid("fn f() { let (a, b = value; }");
    };

    "Parser statement: range loops preserve recursive targets and Write markers"_test =
        [] static noexcept {
            const auto tree = parse_valid(
                "fn f() { for (a, (_, b)): (i32, ((), bool)) in values {} for &(c,) in singles {} const for () in units {} }"
            );
            const auto& first =
                get<ASTRangeForHeader>(get<ASTForStmt>(statement_at(tree, 0uz)).header);
            const auto& target = *get<UniqueIndirect<ASTTupleBindingTarget>>(first.target);
            if (!expect_equal(target.elements.size(), 2uz)) {
                return;
            }
            const auto& nested = *get<UniqueIndirect<ASTTupleBindingTarget>>(target.elements[1]);
            if (!expect_equal(nested.elements.size(), 2uz)) {
                return;
            }
            expect(is<ASTDiscardBindingTarget>(nested.elements[0]));
            expect(first.type.has_value());
            const auto& second =
                get<ASTRangeForHeader>(get<ASTForStmt>(statement_at(tree, 1uz)).header);
            expect(second.write_marker.has_value());
            expect_equal(
                (*get<UniqueIndirect<ASTTupleBindingTarget>>(second.target)).elements.size(),
                1uz
            );
            const auto& unrolled = get<ASTForStmt>(statement_at(tree, 2uz));
            expect(unrolled.const_span.has_value());
            expect((*get<UniqueIndirect<ASTTupleBindingTarget>>(
                        get<ASTRangeForHeader>(unrolled.header).target
                    ))
                       .elements.empty());
            check_invalid("fn f() { for (a,,b) in values {} }");
            check_invalid("fn f() { for (a, 1) in values {} }");
        };

    "Parser statement: cancel names one lexical child"_test = [] static noexcept {
        static constexpr auto text = std::string_view("async fn f() { cancel child; }");
        const auto tree = parse_valid(text);
        const auto& cancellation = get<ASTCancelStmt>(statement_at(tree, 0));
        expect_equal(slice(text, cancellation.keyword_span), std::string_view("cancel"));
        const auto& child = tree.view().expression(cancellation.child);
        expect(is<ASTNameExpr>(child));
        expect_equal(slice(text, child.span), std::string_view("child"));
        for (const auto source : {
                 "async fn f() { cancel(child); }",
                 "async fn f() { cancel &child; }",
                 "async fn f() { cancel object.child; }",
                 "async fn f() { cancel child }",
             }) {
            check_rejected(source);
        }
    };

    "Parser statement: else-if chains use an ordered branch list"_test = [] static noexcept {
        static constexpr auto text = std::string_view(
            "fn classify(value: i32) {\n"
            " if value == 0 { return; }\n"
            " else if value == 1 { return; }\n"
            " else if value == 2 { return; }\n"
            " else { return; }\n"
            "}\n"
        );
        const auto result = parse_valid(text);
        const auto ast = result.view();
        const auto& conditional = get<ASTIfForm>(statement_at(result, 0));
        if (!expect_equal(conditional.branches.size(), 3uz)) {
            return;
        }
        if (!expect(conditional.else_branch.has_value())) {
            return;
        }
        expect_equal(
            slice(text, ast.expression(conditional.branches[0].condition).span),
            std::string_view("value == 0")
        );
        expect_equal(
            slice(text, ast.expression(conditional.branches[1].condition).span),
            std::string_view("value == 1")
        );
        expect_equal(
            slice(text, ast.expression(conditional.branches[2].condition).span),
            std::string_view("value == 2")
        );
    };

    "Parser statement: declarations, actions, and loop headers remain distinct"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "fn actions(values) {\n"
                " let first: i32 = 1; target += first; ++target;\n"
                " while ready { tick(); }\n"
                " for &item: Item in values { mutate(item); }\n"
                " for var i: i32 = 0; i < 10; i += 1, ++count, tick() { consume(i); }\n"
                "}\n"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            if (!expect_equal(body_of(result).statements.size(), 6uz)) {
                return;
            }

            const auto& declaration = get<ASTVariableDecl>(statement_at(result, 0));
            expect_equal(declaration.kind, ASTBindingKind::Let);
            expect(declaration.type.has_value());

            const auto& assignment = get<ASTAssignment>(statement_at(result, 1));
            expect_equal(assignment.op, ASTAssignmentOperator::Add);
            expect(is<ASTNameExpr>(ast.expression(assignment.target)));

            const auto& update = get<ASTUpdate>(statement_at(result, 2));
            expect_equal(update.op, ASTUpdateOperator::Increment);
            expect(is<ASTWhileStmt>(statement_at(result, 3)));

            const auto& range_loop = get<ASTForStmt>(statement_at(result, 4));
            const auto& range = get<ASTRangeForHeader>(range_loop.header);
            if (!expect(range.write_marker.has_value())) {
                return;
            }
            expect_equal(slice(text, *range.write_marker), std::string_view("&"));
            expect(range.type.has_value());
            expect(is<ASTNameExpr>(ast.expression(range.iterable)));

            const auto& c_style_loop = get<ASTForStmt>(statement_at(result, 5));
            const auto& c_style = get<ASTCStyleForHeader>(c_style_loop.header);
            expect(is<ASTVariableDecl>(c_style.initializer));
            if (!expect_equal(c_style.steps.size(), 3uz)) {
                return;
            }
            expect(is<ASTAssignment>(c_style.steps[0]));
            expect(is<ASTUpdate>(c_style.steps[1]));
            expect(is<ASTExprID>(c_style.steps[2]));
        };

    "Parser statement: range-for sources distinguish containers and half-open bounds"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "fn ranges(items, begin, end) {\n"
                " for item in items {} for i in 1..10 {}\n"
                " for &i in begin..end() {} for i in (begin + 1)..(end - 1) {}\n"
                "}\n"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();

            const auto header_at = [&](std::size_t index) noexcept -> const ASTRangeForHeader& {
                return get<ASTRangeForHeader>(get<ASTForStmt>(statement_at(result, index)).header);
            };
            const auto& container = header_at(0);
            const auto container_id = container.iterable;
            expect(is<ASTNameExpr>(ast.expression(container_id)));
            expect_equal(slice(text, ast.expression(container_id).span), std::string_view("items"));

            const auto& literal = get<ASTRangeExpr>(ast.expression(header_at(1).iterable));
            expect(is<ASTLiteral>(ast.expression(literal.begin)));
            expect(is<ASTLiteral>(ast.expression(literal.end)));
            expect_equal(slice(text, literal.operator_span), std::string_view(".."));

            const auto& variable = get<ASTRangeExpr>(ast.expression(header_at(2).iterable));
            expect(is<ASTNameExpr>(ast.expression(variable.begin)));

            check_invalid("fn invalid(values) { for &&value in values {} }");
            expect(is<ASTCallExpr>(ast.expression(variable.end)));

            const auto& grouped = get<ASTRangeExpr>(ast.expression(header_at(3).iterable));
            expect(is<ASTGroupExpr>(ast.expression(grouped.begin)));
            expect(is<ASTGroupExpr>(ast.expression(grouped.end)));
        };

    "Parser statement: control-flow braces win over ungrouped construction"_test =
        [] static noexcept {
            const auto result = parse_valid(
                "fn conditions() { while ready {} while (Flag {}) {} "
                "while (fn() -> bool { predicate }) {} while { break; } }"
            );
            const auto ast = result.view();
            const auto& plain = get<ASTWhileStmt>(statement_at(result, 0));
            expect(is<ASTNameExpr>(ast.expression(*plain.condition)));
            expect(ast.block(plain.body).statements.empty());

            const auto& named = get<ASTWhileStmt>(statement_at(result, 1));
            const auto& named_group = get<ASTGroupExpr>(ast.expression(*named.condition));
            expect(is<ASTConstructionExpr>(ast.expression(named_group.expression)));

            const auto& typed = get<ASTWhileStmt>(statement_at(result, 2));
            const auto& typed_group = get<ASTGroupExpr>(ast.expression(*typed.condition));
            const auto& construction =
                get<ASTConstructionExpr>(ast.expression(typed_group.expression));
            expect(is<ASTFunctionType>(*construction.type));

            const auto& unconditional = get<ASTWhileStmt>(statement_at(result, 3));
            expect(!unconditional.condition.has_value());
            expect_equal(ast.block(unconditional.body).statements.size(), 1uz);

            check_invalid("fn f() { while Flag {} {} }");
        };

    "Parser statement: c_style for requires a condition and preserves action kind"_test =
        [] static noexcept {
            const auto result = parse_valid(
                "fn loops() { for ; ready; {} for index = 0; ready; {} "
                "for begin(); ready; tick() {} }"
            );
            const auto header_at = [&](std::size_t index) noexcept -> const ASTCStyleForHeader& {
                return get<ASTCStyleForHeader>(get<ASTForStmt>(statement_at(result, index)).header);
            };
            expect(is<std::monostate>(header_at(0).initializer));
            expect(is<ASTAssignment>(header_at(1).initializer));
            expect(is<ASTExprID>(header_at(2).initializer));
            expect(is<ASTExprID>(header_at(2).steps[0]));
            check_invalid("fn loops() { for ; ; {} }");
            check_invalid("fn loops() { for index = 0; ; ++index {} }");
        };

    "Parser statement: control transfer uses dedicated alternatives"_test = [] static noexcept {
        const auto result = parse_valid("fn transfer() { return value; break; continue; }");
        const auto& returned = get<ASTControlTransfer>(statement_at(result, 0));
        expect_equal(returned.kind, ASTControlTransferKind::Return);
        expect(returned.value.has_value());
        const auto& broken = get<ASTControlTransfer>(statement_at(result, 1));
        expect_equal(broken.kind, ASTControlTransferKind::Break);
        expect(!broken.value.has_value());
        expect(is<ASTControlTransfer>(statement_at(result, 2)));
        check_invalid("fn invalid() { #[cpp] ---\nnative();\n---\n}");
    };

    "Parser statement: builtin names use ordinary calls in every context"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "test \"context\" {\n"
                " check(true, \"root\");\n"
                " if true { require(true); }\n"
                " let nested = []() { check(true); };\n"
                " let value = check(true);\n"
                " fail();\n"
                "}\n"
                "fn production() { check(true); }\n"
            );
            const auto result = parse_valid(text);
            const auto ast = result.view();
            const auto& test = get<ASTTestDecl>(item(result, 0));
            const auto& body = ast.block(test.body);
            if (!expect_equal(body.statements.size(), 5uz)) {
                return;
            }

            const auto& root_statement = get<ASTExprStatement>(ast.statement(body.statements[0]));
            const auto& root_operation =
                get<ASTCallExpr>(ast.expression(root_statement.expression));
            if (!expect_equal(root_operation.arguments.size(), 2uz)) {
                return;
            }

            const auto& conditional = get<ASTIfForm>(ast.statement(body.statements[1]));
            const auto& branch = ast.branch_block(conditional.branches.front().body);
            if (!expect_equal(branch.statements.size(), 1uz)) {
                return;
            }
            expect(is<ASTExprStatement>(ast.statement(branch.statements.front())));

            const auto& nested = get<ASTVariableDecl>(ast.statement(body.statements[2]));
            if (!expect(nested.initializer.has_value())) {
                return;
            }
            const auto& lambda = get<ASTLambdaExpr>(ast.expression(*nested.initializer));
            const auto& lambda_statement =
                ast.statement(ast.block(get<ASTBlockID>(lambda.body)).statements.front());
            expect(is<ASTExprStatement>(lambda_statement));

            const auto& value = get<ASTVariableDecl>(ast.statement(body.statements[3]));
            if (!expect(value.initializer.has_value())) {
                return;
            }
            expect(is<ASTCallExpr>(ast.expression(*value.initializer)));
            expect(is<ASTExprStatement>(ast.statement(body.statements[4])));

            const auto& production = get<ASTFunctionDecl>(item(result, 1));
            const auto& production_body =
                ast.block(get<ASTBlockID>(get<ASTFunctionBody>(production.implementation).body));
            const auto& production_statement = ast.statement(production_body.statements.front());
            expect(is<ASTExprStatement>(production_statement));

            check_invalid("test \"block\" { { check(true); } }");
        };

    "Parser statement: malformed c_style for expressions report without crashing"_test =
        [] static noexcept {
            check_invalid("fn loops() { for * ; ready; tick() {} }");
            check_invalid("fn loops() { for value; ready; * {} }");
        };

    "Parser statement: discard targets cover bindings and range loops"_test = [] static noexcept {
        static constexpr auto text = std::string_view(
            "fn discard(values: [i32; 2]) {"
            " let _ = 1; var _ = 2; const _ = 3;"
            " let _name = 4; for _ in values {} for &_ in values {}"
            "}"
        );
        const auto result = parse_valid(text);
        if (!expect_equal(body_of(result).statements.size(), 6uz)) {
            return;
        }
        for (auto index = 0uz; index < 3; ++index) {
            expect(
                is<ASTDiscardBindingTarget>(
                    get<ASTVariableDecl>(statement_at(result, index)).target
                )
            );
        }
        const auto& named = get<ASTVariableDecl>(statement_at(result, 3));
        expect(is<ASTNamedBindingTarget>(named.target));
        expect_equal(
            slice(text, get<ASTNamedBindingTarget>(named.target).name_span),
            std::string_view("_name")
        );
        const auto& value_range =
            get<ASTRangeForHeader>(get<ASTForStmt>(statement_at(result, 4)).header);
        const auto& reference_range =
            get<ASTRangeForHeader>(get<ASTForStmt>(statement_at(result, 5)).header);
        expect(is<ASTDiscardBindingTarget>(value_range.target));
        expect(is<ASTDiscardBindingTarget>(reference_range.target));
        expect(!value_range.write_marker.has_value());
        expect(reference_range.write_marker.has_value());
    };

    "Parser statement: ranges require value endpoints and a closed upper bound"_test =
        [] static noexcept {
            const auto invalid = std::array {
                "fn f() { let r = ..10; }",
                "fn f() { let r = 0..; }",
                "fn f() { let r = 0..1..2; }",
                "fn f(x: i32) { match x { 0..= => {}, _ => {} } }",
                "fn f(x: i32) { match x { .. => {}, _ => {} } }"
            };
            each(invalid, std::identity {}, [](const auto& source) static noexcept {
                check_invalid(source);
            });
        };

    "Parser: const blocks compose with constants functions tests and nested statements"_test =
        [] static noexcept {
            static constexpr auto text = std::string_view(R"(
        const "module" { const "nested" { println("nested"); } }
        const answer = 42;
        const fn compute() -> i32 { const "local" {} return answer; }
        const test "answer" { const "test body" {} check(compute() == answer); }
        const {}
    )");
            const auto tree = parse_valid(text);
            const auto ast = tree.view();
            if (!expect(root(tree).items.size() == 5uz)) {
                return;
            }
            const auto& block = get<ASTConstBlock>(item(tree, 0));
            if (!expect(block.label.has_value())) {
                return;
            }
            expect_equal(block.label->text, std::string_view("module"));
            expect_equal(slice(text, block.label->span), std::string_view("\"module\""));
            if (!expect(ast.block(block.body).statements.size() == 1uz)) {
                return;
            }
            const auto& nested =
                get<ASTConstBlock>(ast.statement(ast.block(block.body).statements.front()));
            if (!expect(nested.label.has_value())) {
                return;
            }
            expect_equal(nested.label->text, std::string_view("nested"));
            expect(is<ASTConstantDecl>(item(tree, 1)));
            expect(get<ASTFunctionDecl>(item(tree, 2)).const_span.has_value());
            expect(get<ASTTestDecl>(item(tree, 3)).is_const);
            expect(!(get<ASTConstBlock>(item(tree, 4)).label.has_value()));

            check_invalid("const \"label\";", "expected '{'");
            check_invalid("fn f() { const \"label\"; }", "expected '{'");
            check_invalid("private const \"label\" {}", "expected constant name");
            check_invalid("export const \"label\" {}", "expected constant name");
        };
});

} // namespace
