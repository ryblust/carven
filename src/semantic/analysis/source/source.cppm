module carven:semantic.analysis.source;

import :semantic.semir.ids;
import :semantic.semir.type;
import :source.standard;
import :source.text;
import :support.function_ref;
import std;

// Builtin values describe construction. TypeIDs are emitted only with a
// successfully published program and belong to that same program owner.
using SourceType = std::variant<BuiltinType, TypeID>;
using SourceEntity = std::variant<SourceSpan, ProvidedExport>;

struct SourceCallableInformation final {
    std::vector<std::string> parameters;
    std::string result;
    // A callable value's proven source target, separate from its binding entity.
    // Signature-only views retain no target unless construction proves one.
    std::optional<SourceSpan> target;
};

struct SourceOccurrence final {
    SourceSpan location;
    std::optional<SourceType> type;
    std::optional<SourceEntity> entity;
    std::optional<SourceSpan> alias;
    std::string type_description;
    std::optional<SourceCallableInformation> callable;
    std::string documentation;
    // Navigation to an explicit native import binding, not a C++ entity.
    std::optional<SourceSpan> external_binding;

    auto definition() const noexcept -> std::optional<SourceSpan>;
};

// Detached name/type descriptions for an explicit source query. Construction
// identities never escape through this result, including when the body fails.
enum class SourceCompletionKind {
    Value,
    Function,
    Variable,
    Constant,
    Structure,
    Enumeration,
    EnumCase,
    Field,
    Method,
    Module,
    Keyword
};

struct SourceCompletionCandidate final {
    std::string name;
    SourceCompletionKind kind;
    std::string detail;
};

enum class SourceCompletionContext { None, Name, Type, Member };

using SourceCompletionOutput =
    FunctionRef<void(std::span<const SourceCompletionCandidate>, SourceCompletionContext) noexcept>;

struct SourceCompletionRequest final {
    std::optional<SourceSpan> position;
    SourceCompletionOutput output;
    std::span<const SourceSpan> excluded;
};

// Detached source facts can be complete even when later validation rejects
// publication. Completeness covers construction and solving for these inputs; it never grants access to an unvalidated SemIRProgram.
struct SourceAnalysisView final {
    std::span<const SourceOccurrence> occurrences;
    bool construction_complete;
};

// One synchronous observation per semantic analysis. The span borrows temporary
// storage. Recipients copy retained values and keep any accompanying program alive.
// Declaration/nominal failures contribute no occurrences. Failed bodies retain
// detached evidence from their successful portions, including nested closures.
// These observations do not publish a body. Definitions do not require a type.
using SourceAnalysisOutput = FunctionRef<void(SourceAnalysisView) noexcept>;
