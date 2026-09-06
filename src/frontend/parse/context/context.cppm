module carven:frontend.parse.context;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.interop;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.ast.type;
import :frontend.lex.token;
import :frontend.literal;
import :frontend.parse.builder;
import :source.text;
import std;

class Parser final {
public:
    Parser(SourceView source_view, const TokenBuffer& token_buffer) noexcept;
    auto run() noexcept -> std::expected<SyntaxTree, Diagnostics>;

private:
    struct ParseFailure final {
        Span span;
        std::string message;
        DiagnosticCode code;
    };

    struct Checkpoint final {
        std::size_t cursor;
        bool split_right_shift;
        ASTBuilder::Checkpoint builder;
        bool failed;
        std::size_t diagnostic_count;
        std::uint32_t expression_nesting;
        std::uint32_t block_boundary_depth;
        std::uint32_t syntax_nesting;
    };

    struct ParsedNumericLiteral final {
        Span span;
        NumericLiteralValue value;
    };

    struct ParsedStringLiteral final {
        Span span;
        StringLiteralValue value;
    };

    template<typename Value>
    struct ParsedTypeForm final {
        Span span;
        Value value;
    };

    std::string_view source;
    SourceID source_id;
    const TokenBuffer* token_buffer;
    std::span<const Token> tokens;
    std::size_t cursor = 0;
    bool split_right_shift = false;
    ASTBuilder builder;
    Diagnostics diagnostics;
    std::vector<std::optional<ParseFailure>> speculation_failures;
    std::optional<ParseFailure> furthest_speculative_failure;
    bool failed = false;
    std::uint32_t speculation_depth = 0;
    std::uint32_t expression_nesting = 0;
    std::uint32_t block_boundary_depth = 0;
    std::uint32_t syntax_nesting = 0;
    bool test_statements_enabled = false;

    static constexpr std::uint32_t maximum_syntax_nesting = 512;
    class DepthGuard final {
    public:
        DepthGuard(const DepthGuard&) = delete;
        DepthGuard(DepthGuard&&) = delete;
        auto operator=(const DepthGuard&) -> DepthGuard& = delete;
        auto operator=(DepthGuard&&) -> DepthGuard& = delete;
        ~DepthGuard() noexcept;
        explicit operator bool() const noexcept;

    private:
        explicit DepthGuard(std::uint32_t* depth) noexcept;
        std::uint32_t* depth;

        friend class Parser;
    };

    class TestStatementContextGuard final {
    public:
        TestStatementContextGuard(const TestStatementContextGuard&) = delete;
        auto operator=(const TestStatementContextGuard&) noexcept
            -> TestStatementContextGuard& = delete;
        ~TestStatementContextGuard() noexcept;

    private:
        TestStatementContextGuard(bool& context, bool enabled) noexcept;

        bool* context;
        bool prior;

        friend class Parser;
    };

    static auto enter_depth(std::uint32_t& depth) noexcept -> DepthGuard;
    auto enter_syntax_nesting() noexcept -> DepthGuard;
    auto enter_test_statement_context(bool enabled) noexcept -> TestStatementContextGuard;
    auto preflight_delimiter_nesting() noexcept -> bool;
    auto synchronize_top_level_item() noexcept -> void;

    auto at_end() const noexcept -> bool;
    auto current() const noexcept -> Token;
    auto check(TokenKind kind) const noexcept -> bool;
    auto check_next(TokenKind kind) const noexcept -> bool;
    auto consume() noexcept -> Token;
    auto match(TokenKind kind) noexcept -> std::optional<Token>;
    auto fail(
        std::string_view message,
        Span span,
        DiagnosticCode code = DiagnosticCode::Syntax
    ) noexcept -> void;
    auto fail_here(std::string_view message) noexcept -> void;
    auto expect(TokenKind kind, std::string_view message) noexcept -> Token;
    auto save() const noexcept -> Checkpoint;
    auto restore(const Checkpoint& checkpoint) noexcept -> void;
    auto begin_speculation() noexcept -> Checkpoint;
    auto finish_speculation(const Checkpoint& checkpoint, bool commit) noexcept -> void;
    auto remember_speculative_failure(ParseFailure failure) noexcept -> void;
    static auto join(Span first, Span last) noexcept -> Span;
    auto parse_module_reference() noexcept -> ASTModuleReference;
    auto parse_cpp_header_import() noexcept -> ASTCppHeaderImport;
    auto parse_module_import() noexcept -> ASTModuleImportID;
    auto parse_top_level_item() noexcept -> std::optional<ASTItemID>;
    auto parse_enum(ASTDeclarationVisibility visibility) noexcept
        -> std::optional<std::pair<Span, ASTEnumDecl>>;
    auto parse_struct(ASTDeclarationVisibility visibility) noexcept
        -> std::optional<std::pair<Span, ASTStructDecl>>;
    auto parse_cpp_declaration_form(Token keyword) noexcept -> Span;
    auto parse_function(
        ASTDeclarationVisibility visibility,
        std::optional<ASTCppExportForm> cpp_export,
        std::optional<Span> cpp_import
    ) noexcept -> std::optional<std::pair<Span, ASTFunctionDecl>>;
    auto parse_constant(ASTDeclarationVisibility visibility) noexcept
        -> std::optional<std::pair<Span, ASTConstantDecl>>;
    auto parse_test() noexcept -> std::optional<std::pair<Span, ASTTestDecl>>;

    auto parse_type() noexcept -> std::optional<ASTTypeID>;
    auto parse_named_type() noexcept -> std::optional<ASTTypeID>;
    auto parse_array_type() noexcept -> std::optional<ASTTypeID>;
    auto parse_function_type() noexcept -> std::optional<ASTTypeID>;
    auto parse_named_type_form() noexcept -> ParsedTypeForm<ASTNamedType>;
    auto parse_array_type_form() noexcept -> std::optional<ParsedTypeForm<ASTArrayType>>;
    auto parse_function_type_form() noexcept -> std::optional<ParsedTypeForm<ASTFunctionType>>;
    auto parse_throw_clause() noexcept -> ASTThrowClause;
    auto parse_ordinary_block() noexcept -> std::optional<ASTBlockID>;
    auto parse_branch_block() noexcept -> std::optional<ASTBranchBlockID>;
    auto starts_unambiguous_statement() const noexcept -> bool;
    auto test_operation_starts_here() const noexcept -> bool;
    auto parse_test_operation_statement() noexcept -> std::optional<ASTStmtID>;
    auto parse_statement() noexcept -> std::optional<ASTStmtID>;
    auto parse_variable_declaration_head() noexcept -> std::optional<ASTVariableDecl>;
    static auto assignment_operator(TokenKind kind) noexcept
        -> std::optional<ASTAssignmentOperator>;
    auto finish_assignment(ASTExprID target) noexcept -> std::optional<ASTAssignment>;
    auto parse_update() noexcept -> std::optional<ASTUpdate>;
    auto parse_control_transfer(bool with_semicolon) noexcept -> std::optional<ASTControlTransfer>;
    auto parse_while_statement() noexcept -> std::optional<ASTStmtID>;
    auto parse_for_statement() noexcept -> std::optional<ASTStmtID>;
    auto parse_for_header() noexcept -> std::optional<ASTForHeader>;
    auto parse_for_step() noexcept -> std::optional<ASTForStep>;

    auto parse_expression() noexcept -> std::optional<ASTExprID>;
    template<typename ParseOperand>
    auto parse_left_associative(
        ParseOperand parse_operand,
        std::span<const std::pair<TokenKind, ASTBinaryOperator>> operators
    ) noexcept -> std::optional<ASTExprID>;
    auto parse_logical_or() noexcept -> std::optional<ASTExprID>;
    auto parse_logical_and() noexcept -> std::optional<ASTExprID>;
    auto parse_bitwise_or() noexcept -> std::optional<ASTExprID>;
    auto parse_bitwise_xor() noexcept -> std::optional<ASTExprID>;
    auto parse_bitwise_and() noexcept -> std::optional<ASTExprID>;
    static auto comparison_operator(TokenKind kind) noexcept -> std::optional<ASTBinaryOperator>;
    auto parse_comparison() noexcept -> std::optional<ASTExprID>;
    auto parse_shift() noexcept -> std::optional<ASTExprID>;
    auto parse_additive() noexcept -> std::optional<ASTExprID>;
    auto parse_multiplicative() noexcept -> std::optional<ASTExprID>;
    auto parse_cast_expression() noexcept -> std::optional<ASTExprID>;
    auto parse_prefix_expression() noexcept -> std::optional<ASTExprID>;
    auto parse_postfix_expression() noexcept -> std::optional<ASTExprID>;
    auto parse_call(ASTExprID callee) noexcept -> std::optional<ASTExprID>;
    auto parse_primary_expression() noexcept -> std::optional<ASTExprID>;
    auto lambda_starts_here() const noexcept -> bool;
    auto parse_lambda_expression() noexcept -> std::optional<ASTExprID>;
    auto construction_allowed_here() const noexcept -> bool;
    auto try_parse_construction() noexcept -> std::optional<ASTExprID>;

    auto parse_expression_before_block() noexcept -> std::optional<ASTExprID>;
    auto parse_if_form() noexcept -> std::optional<ASTIfForm>;
    auto parse_match_form() noexcept -> std::optional<ASTMatchForm>;
    auto parse_match_arm() noexcept -> std::optional<ASTMatchArm>;
    auto parse_try_form() noexcept -> std::optional<ASTTryForm>;
    auto parse_catch_arm() noexcept -> std::optional<ASTCatchArm>;
    auto parse_catch_pattern() noexcept -> std::optional<ASTCatchPattern>;
    auto parse_catch_pattern_atom() noexcept -> std::optional<ASTCatchPatternAtom>;
    auto parse_pattern() noexcept -> std::optional<ASTPatternID>;
    auto parse_primary_pattern() noexcept -> std::optional<ASTPatternID>;
    auto finish_case_pattern(Span start, ASTCaseQualifier qualifier, Span name_span) noexcept
        -> std::optional<ASTPatternID>;
    auto parse_qualified_name() noexcept -> ASTQualifiedName;
    auto consume_literal() noexcept -> ASTLiteral;
    auto expect_numeric_literal(std::string_view message) noexcept
        -> std::optional<ParsedNumericLiteral>;
    auto expect_string_literal(std::string_view message) noexcept
        -> std::optional<ParsedStringLiteral>;
    auto make_cpp_source_fragment(Span full_span) const noexcept -> ASTCppSourceFragment;
};
