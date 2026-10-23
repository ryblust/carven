module carven:workspace.folding;

import :frontend.document;
import :source.text;
import std;

enum class WorkspaceFoldKind { Code, Comment, Imports };

// Half-open hidden byte ranges; closing delimiters remain outside the range.
struct WorkspaceFold final {
    Span range;
    WorkspaceFoldKind kind;
};

using WorkspaceFoldList = std::vector<WorkspaceFold>;

auto collect_workspace_folds(const ParsedDocument& document) noexcept -> WorkspaceFoldList;
