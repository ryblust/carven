module carven:workspace.decorations;

import :source.text;
import :workspace.semantic;
import std;

class WorkspaceAnalysisSnapshot;

enum class WorkspaceHighlightKind {
    Type,
    Structure,
    Class,
    Enumeration,
    TypeParameter,
    Parameter,
    Variable,
    Property,
    EnumMember,
    Function,
    Method
};

// Declaration, readonly, static, async and default-library flags.
enum class WorkspaceHighlightModifier : std::uint32_t {
    Declaration = 1,
    Readonly = 2,
    Static = 4,
    Async = 8,
    DefaultLibrary = 16
};

struct WorkspaceHighlight final {
    Span range;
    WorkspaceHighlightKind kind;
    std::uint32_t modifiers;
};

enum class WorkspaceHintKind { Type, Parameter };

struct WorkspaceInlayHint final {
    std::uint32_t position;
    std::string label;
    WorkspaceHintKind kind;
};

struct WorkspaceDecorations final {
    std::vector<WorkspaceHighlight> highlights;
    std::vector<WorkspaceInlayHint> hints;
};

auto collect_workspace_decorations(
    const WorkspaceAnalysisSnapshot& snapshot,
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    Span visible,
    const WorkspaceSemanticAnalysis& analysis
) noexcept -> WorkspaceDecorations;
