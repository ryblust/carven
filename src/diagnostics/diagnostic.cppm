module carven:diagnostics.diagnostic;

import :diagnostics.code;
import :source.text;
import std;

struct DiagnosticNote final {
    std::string message;
    std::optional<SourceSpan> span;
};

struct DiagnosticLabel final {
    SourceSpan span;
    std::string message;
};

struct DiagnosticFinding final {
    DiagnosticSeverity severity;
    DiagnosticCode code;
    std::string message;
};

struct DiagnosticAttachment final {
    std::optional<DiagnosticLabel> primary;
    std::vector<DiagnosticLabel> related;
    std::vector<DiagnosticNote> notes;
    // Each entry suggests a source change for the rejected contract.
    std::vector<std::string> helps;
};

struct Diagnostic final {
    DiagnosticFinding finding;
    DiagnosticAttachment attachment;
};

using Diagnostics = std::vector<Diagnostic>;
