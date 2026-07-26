module carven:diagnostics.diagnosed;

import :diagnostics.code;
import :diagnostics.diagnostic;
import std;

template<typename T>
struct Diagnosed final {
    T value;
    Diagnostics diagnostics;
};

template<typename T>
auto has_errors(const Diagnosed<T>& result) noexcept -> bool {
    return std::ranges::any_of(
        result.diagnostics,
        [](const Diagnostic& diagnostic) static noexcept -> bool {
            return diagnostic.finding.severity == DiagnosticSeverity::Error;
        }
    );
}
