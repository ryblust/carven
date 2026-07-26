module carven:semantic.analysis.elaboration.types;

import :frontend.ast.literal;
import :frontend.ast.type;
import :frontend.literal;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.resolution;
import :semantic.hir.access;
import std;

struct LiteralFact final {
    std::optional<HIRConstant> constant;
    HIRLiteralValue value;
};

struct ResolvedEnumCase final {
    EnumCaseID id;
    SymbolID symbol;
    std::span<const HIRTypeID> payload_types;
    std::optional<HIRConstantID> constant;
};

struct ResolvedStructure final {
    StructID id;
    std::span<const HIRStructField> fields;
};

enum class ValueTypeRole {
    ArrayElement,
    Binding,
    Constant,
    EnumPayload,
    FunctionParameter,
    MatchSubject,
    StructureField,
};

auto access_mode(ASTAccessSyntax access) noexcept -> HIRAccessMode;

auto symbol_for(
    ModuleAnalysis& module_analysis,
    const ScopeStack& scopes,
    std::string_view name,
    Span use_span
) noexcept -> LookupResult<SymbolID>;

auto symbol_type(ModuleAnalysis& module_analysis, SymbolID symbol) noexcept -> HIRTypeID;

auto set_symbol_type(ModuleAnalysis& module_analysis, SymbolID symbol, HIRTypeID type) noexcept
    -> void;

auto nominal_symbol(const ModuleAnalysis& module_analysis, HIRTypeID type_id) noexcept
    -> std::optional<SymbolID>;

auto resolve_structure_contract(
    ModuleAnalysis& module_analysis,
    HIRTypeID type_id,
    Span origin
) noexcept -> LookupResult<ResolvedStructure>;

auto resolve_enum_contract(ModuleAnalysis& module_analysis, HIRTypeID type_id, Span origin) noexcept
    -> LookupResult<EnumID>;

auto resolve_qualified_value(
    ModuleAnalysis& module_analysis,
    const ScopeStack& scopes,
    const ASTQualifiedName& qualified
) noexcept -> LookupResult<SymbolID>;

auto builtin(ModuleAnalysis& module_analysis, Span span, HIRBuiltinType kind) noexcept -> HIRTypeID;

auto error_type(ModuleAnalysis& module_analysis, Span span) noexcept -> HIRTypeID;

auto require_value_type(
    ModuleAnalysis& module_analysis,
    HIRTypeID type,
    Span span,
    ValueTypeRole role
) noexcept -> HIRTypeID;

auto foreign_type(ModuleAnalysis& module_analysis, Span span) noexcept -> HIRTypeID;

auto build_type(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    ASTTypeID id
) noexcept -> HIRTypeID;

auto literal_type(ModuleAnalysis& module_analysis, const ASTLiteral& literal) noexcept -> HIRTypeID;

auto as_numeric_literal(const ASTLiteral& literal) noexcept -> std::optional<NumericLiteralValue>;

auto numeric_literal_type(
    ModuleAnalysis& module_analysis,
    Span span,
    const NumericLiteralValue& value
) noexcept -> HIRTypeID;

auto literal_fact(
    ModuleAnalysis& module_analysis,
    const ASTLiteral& literal,
    HIRTypeID resolved_type
) noexcept -> LiteralFact;

auto negative_literal_fact(
    ModuleAnalysis& module_analysis,
    Span span,
    const NumericLiteralValue& value,
    HIRTypeID resolved_type
) noexcept -> LiteralFact;

auto construction_type(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTConstructionType& source_type
) noexcept -> HIRTypeID;

auto array_extent(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    ASTExprID expression_id
) noexcept -> std::optional<std::uint64_t>;

auto integer_constant_fits(
    const ModuleAnalysis& module_analysis,
    HIRIntegerConstant constant,
    HIRTypeID id
) noexcept -> bool;

auto compatible(const ModuleAnalysis& module_analysis, HIRTypeID left, HIRTypeID right) noexcept
    -> bool;

auto defer_callable_compatibility(
    ModuleAnalysis& module_analysis,
    HIRExprID source,
    std::variant<HIRTypeID, HIRExprID> target,
    ProgramOriginID origin,
    DiagnosticCode code,
    std::string_view message
) noexcept -> void;

auto normalized_failures(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTThrowClause& clause
) noexcept -> std::vector<HIRTypeID>;

auto is_void(const ModuleAnalysis& module_analysis, HIRTypeID id) noexcept -> bool;

auto is_bool(const ModuleAnalysis& module_analysis, HIRTypeID id) noexcept -> bool;

auto is_integer(const ModuleAnalysis& module_analysis, HIRTypeID id) noexcept -> bool;

auto is_numeric(const ModuleAnalysis& module_analysis, HIRTypeID id) noexcept -> bool;

auto is_opaque_or_error(const ModuleAnalysis& module_analysis, HIRTypeID id) noexcept -> bool;

auto supports_equality(const ModuleAnalysis& module_analysis, HIRTypeID id) noexcept -> bool;

auto resolve_enum_case(
    ModuleAnalysis& module_analysis,
    HIRTypeID enum_type,
    std::string_view name,
    Span origin
) noexcept -> LookupResult<ResolvedEnumCase>;
