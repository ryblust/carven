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
import :test.harness.framework;
import :test.internal.frontend.parse.fixture;
import std;

namespace {

namespace ct = carven::testing;

auto body_of(const SyntaxTree& tree) noexcept -> const ASTBlock& {
    return function_body(tree);
}

auto statement_at(const SyntaxTree& tree, std::size_t index) noexcept -> const ASTStmt& {
    return tree.view().statement(body_of(tree).statements[index]);
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test("Parser statement: else-if chains use an ordered branch list", [] static noexcept {
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
        if (!ct::expect_equal(conditional.branches.size(), 3uz)) {
            return;
        }
        if (!ct::expect(conditional.else_branch.has_value())) {
            return;
        }
        ct::expect_equal(
            slice(text, ast.expression(conditional.branches[0].condition).span),
            std::string_view("value == 0")
        );
        ct::expect_equal(
            slice(text, ast.expression(conditional.branches[1].condition).span),
            std::string_view("value == 1")
        );
        ct::expect_equal(
            slice(text, ast.expression(conditional.branches[2].condition).span),
            std::string_view("value == 2")
        );
    });

    ct::test(
        "Parser statement: declarations, actions, and loop headers remain distinct",
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
            if (!ct::expect_equal(body_of(result).statements.size(), 6uz)) {
                return;
            }

            const auto& declaration = get<ASTVariableDecl>(statement_at(result, 0));
            ct::expect_equal(declaration.kind, ASTBindingKind::Let);
            ct::expect(declaration.type.has_value());

            const auto& assignment = get<ASTAssignment>(statement_at(result, 1));
            ct::expect_equal(assignment.op, ASTAssignmentOperator::Add);
            ct::expect(is<ASTNameExpr>(ast.expression(assignment.target)));

            const auto& update = get<ASTUpdate>(statement_at(result, 2));
            ct::expect_equal(update.op, ASTUpdateOperator::Increment);
            ct::expect(is<ASTWhileStmt>(statement_at(result, 3)));

            const auto& range_loop = get<ASTForStmt>(statement_at(result, 4));
            const auto& range = get<ASTRangeForHeader>(range_loop.header);
            if (!ct::expect(range.write_marker.has_value())) {
                return;
            }
            ct::expect_equal(slice(text, *range.write_marker), std::string_view("&"));
            ct::expect(range.type.has_value());
            ct::expect(is<ASTNameExpr>(ast.expression(range.iterable)));

            const auto& c_style_loop = get<ASTForStmt>(statement_at(result, 5));
            const auto& c_style = get<ASTCStyleForHeader>(c_style_loop.header);
            ct::expect(is<ASTVariableDecl>(c_style.initializer));
            if (!ct::expect_equal(c_style.steps.size(), 3uz)) {
                return;
            }
            ct::expect(is<ASTAssignment>(c_style.steps[0]));
            ct::expect(is<ASTUpdate>(c_style.steps[1]));
            ct::expect(is<ASTExprID>(c_style.steps[2]));
        }
    );

    ct::test(
        "Parser statement: range-for sources distinguish containers and half-open bounds",
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
            ct::expect(is<ASTNameExpr>(ast.expression(container_id)));
            ct::expect_equal(
                slice(text, ast.expression(container_id).span),
                std::string_view("items")
            );

            const auto& literal = get<ASTRangeExpr>(ast.expression(header_at(1).iterable));
            ct::expect(is<ASTLiteral>(ast.expression(literal.begin)));
            ct::expect(is<ASTLiteral>(ast.expression(literal.end)));
            ct::expect_equal(slice(text, literal.operator_span), std::string_view(".."));

            const auto& variable = get<ASTRangeExpr>(ast.expression(header_at(2).iterable));
            ct::expect(is<ASTNameExpr>(ast.expression(variable.begin)));

            check_invalid("fn invalid(values) { for &&value in values {} }");
            ct::expect(is<ASTCallExpr>(ast.expression(variable.end)));

            const auto& grouped = get<ASTRangeExpr>(ast.expression(header_at(3).iterable));
            ct::expect(is<ASTGroupExpr>(ast.expression(grouped.begin)));
            ct::expect(is<ASTGroupExpr>(ast.expression(grouped.end)));
        }
    );

    ct::test(
        "Parser statement: control-flow braces win over ungrouped construction",
        [] static noexcept {
            const auto result = parse_valid(
                "fn conditions() { while ready {} while (Flag {}) {} "
                "while (fn() -> bool { predicate }) {} while { break; } }"
            );
            const auto ast = result.view();
            const auto& plain = get<ASTWhileStmt>(statement_at(result, 0));
            ct::expect(is<ASTNameExpr>(ast.expression(*plain.condition)));
            ct::expect(ast.block(plain.body).statements.empty());

            const auto& named = get<ASTWhileStmt>(statement_at(result, 1));
            const auto& named_group = get<ASTGroupExpr>(ast.expression(*named.condition));
            ct::expect(is<ASTConstructionExpr>(ast.expression(named_group.expression)));

            const auto& typed = get<ASTWhileStmt>(statement_at(result, 2));
            const auto& typed_group = get<ASTGroupExpr>(ast.expression(*typed.condition));
            const auto& construction =
                get<ASTConstructionExpr>(ast.expression(typed_group.expression));
            ct::expect(is<ASTFunctionType>(*construction.type));

            const auto& unconditional = get<ASTWhileStmt>(statement_at(result, 3));
            ct::expect(!unconditional.condition.has_value());
            ct::expect_equal(ast.block(unconditional.body).statements.size(), 1uz);

            check_invalid("fn f() { while Flag {} {} }");
        }
    );

    ct::test(
        "Parser statement: c_style for requires a condition and preserves action kind",
        [] static noexcept {
            const auto result = parse_valid(
                "fn loops() { for ; ready; {} for index = 0; ready; {} "
                "for begin(); ready; tick() {} }"
            );
            const auto header_at = [&](std::size_t index) noexcept -> const ASTCStyleForHeader& {
                return get<ASTCStyleForHeader>(get<ASTForStmt>(statement_at(result, index)).header);
            };
            ct::expect(is<std::monostate>(header_at(0).initializer));
            ct::expect(is<ASTAssignment>(header_at(1).initializer));
            ct::expect(is<ASTExprID>(header_at(2).initializer));
            ct::expect(is<ASTExprID>(header_at(2).steps[0]));
            check_invalid("fn loops() { for ; ; {} }");
            check_invalid("fn loops() { for index = 0; ; ++index {} }");
        }
    );

    ct::test("Parser statement: control transfer uses dedicated alternatives", [] static noexcept {
        const auto result = parse_valid("fn transfer() { return value; break; continue; }");
        const auto& returned = get<ASTControlTransfer>(statement_at(result, 0));
        ct::expect_equal(returned.kind, ASTControlTransferKind::Return);
        ct::expect(returned.value.has_value());
        const auto& broken = get<ASTControlTransfer>(statement_at(result, 1));
        ct::expect_equal(broken.kind, ASTControlTransferKind::Break);
        ct::expect(!broken.value.has_value());
        ct::expect(is<ASTControlTransfer>(statement_at(result, 2)));
        check_invalid("fn invalid() { #[cpp] ---\nnative();\n---\n}");
    });

    ct::test(
        "Parser statement: builtin names use ordinary calls in every context",
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
            if (!ct::expect_equal(body.statements.size(), 5uz)) {
                return;
            }

            const auto& root_statement = get<ASTExprStatement>(ast.statement(body.statements[0]));
            const auto& root_operation =
                get<ASTCallExpr>(ast.expression(root_statement.expression));
            if (!ct::expect_equal(root_operation.arguments.size(), 2uz)) {
                return;
            }

            const auto& conditional = get<ASTIfForm>(ast.statement(body.statements[1]));
            const auto& branch = ast.branch_block(conditional.branches.front().body);
            if (!ct::expect_equal(branch.statements.size(), 1uz)) {
                return;
            }
            ct::expect(is<ASTExprStatement>(ast.statement(branch.statements.front())));

            const auto& nested = get<ASTVariableDecl>(ast.statement(body.statements[2]));
            if (!ct::expect(nested.initializer.has_value())) {
                return;
            }
            const auto& lambda = get<ASTLambdaExpr>(ast.expression(*nested.initializer));
            const auto& lambda_statement =
                ast.statement(ast.block(get<ASTBlockID>(lambda.body)).statements.front());
            ct::expect(is<ASTExprStatement>(lambda_statement));

            const auto& value = get<ASTVariableDecl>(ast.statement(body.statements[3]));
            if (!ct::expect(value.initializer.has_value())) {
                return;
            }
            ct::expect(is<ASTCallExpr>(ast.expression(*value.initializer)));
            ct::expect(is<ASTExprStatement>(ast.statement(body.statements[4])));

            const auto& production = get<ASTFunctionDecl>(item(result, 1));
            const auto& production_body =
                ast.block(get<ASTBlockID>(get<ASTFunctionBody>(production.implementation).body));
            const auto& production_statement = ast.statement(production_body.statements.front());
            ct::expect(is<ASTExprStatement>(production_statement));

            check_invalid("test \"block\" { { check(true); } }");
        }
    );

    ct::test(
        "Parser statement: malformed c_style for expressions report without crashing",
        [] static noexcept {
            check_invalid("fn loops() { for * ; ready; tick() {} }");
            check_invalid("fn loops() { for value; ready; * {} }");
        }
    );

    ct::test(
        "Parser statement: discard targets cover bindings and range loops",
        [] static noexcept {
            static constexpr auto text = std::string_view(
                "fn discard(values: [i32; 2]) {"
                " let _ = 1; var _ = 2; const _ = 3;"
                " let _name = 4; for _ in values {} for &_ in values {}"
                "}"
            );
            const auto result = parse_valid(text);
            if (!ct::expect_equal(body_of(result).statements.size(), 6uz)) {
                return;
            }
            for (auto index = 0uz; index < 3; ++index) {
                ct::expect(
                    is<ASTDiscardBindingTarget>(
                        get<ASTVariableDecl>(statement_at(result, index)).target
                    )
                );
            }
            const auto& named = get<ASTVariableDecl>(statement_at(result, 3));
            ct::expect(is<ASTNamedBindingTarget>(named.target));
            ct::expect_equal(
                slice(text, get<ASTNamedBindingTarget>(named.target).name_span),
                std::string_view("_name")
            );
            const auto& value_range =
                get<ASTRangeForHeader>(get<ASTForStmt>(statement_at(result, 4)).header);
            const auto& reference_range =
                get<ASTRangeForHeader>(get<ASTForStmt>(statement_at(result, 5)).header);
            ct::expect(is<ASTDiscardBindingTarget>(value_range.target));
            ct::expect(is<ASTDiscardBindingTarget>(reference_range.target));
            ct::expect(!value_range.write_marker.has_value());
            ct::expect(reference_range.write_marker.has_value());
        }
    );

    ct::test(
        "Parser statement: ranges require value endpoints and a closed upper bound",
        [] static noexcept {
            const auto invalid = std::array {
                "fn f() { let r = ..10; }",
                "fn f() { let r = 0..; }",
                "fn f() { let r = 0..1..2; }",
                "fn f(x: i32) { match x { 0..= => {}, _ => {} } }",
                "fn f(x: i32) { match x { .. => {}, _ => {} } }"
            };
            ct::each(invalid, std::identity {}, [](const auto& source) static noexcept {
                check_invalid(source);
            });
        }
    );

    ct::test(
        "Parser: const blocks compose with constants functions tests and nested statements",
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
            if (!ct::expect(root(tree).items.size() == 5uz)) {
                return;
            }
            const auto& block = get<ASTConstBlock>(item(tree, 0));
            if (!ct::expect(block.label.has_value())) {
                return;
            }
            ct::expect_equal(block.label->text, std::string_view("module"));
            ct::expect_equal(slice(text, block.label->span), std::string_view("\"module\""));
            if (!ct::expect(ast.block(block.body).statements.size() == 1uz)) {
                return;
            }
            const auto& nested =
                get<ASTConstBlock>(ast.statement(ast.block(block.body).statements.front()));
            if (!ct::expect(nested.label.has_value())) {
                return;
            }
            ct::expect_equal(nested.label->text, std::string_view("nested"));
            ct::expect(is<ASTConstantDecl>(item(tree, 1)));
            ct::expect(get<ASTFunctionDecl>(item(tree, 2)).const_span.has_value());
            ct::expect(get<ASTTestDecl>(item(tree, 3)).is_const);
            ct::expect(!(get<ASTConstBlock>(item(tree, 4)).label.has_value()));

            check_invalid("const \"label\";", "expected '{'");
            check_invalid("fn f() { const \"label\"; }", "expected '{'");
            check_invalid("private const \"label\" {}", "expected constant name");
            check_invalid("export const \"label\" {}", "expected constant name");
        }
    );
});

} // namespace
