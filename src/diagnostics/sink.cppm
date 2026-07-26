module carven:diagnostics.sink;

import :diagnostics.code;
import :diagnostics.diagnostic;
import std;

class DiagnosticSink final {
public:
    auto emit(Diagnostic diagnostic) noexcept -> void;
    auto emit(std::span<const Diagnostic> diagnostics) noexcept -> void;
    auto empty() const noexcept -> bool;
    auto size() const noexcept -> std::size_t;
    auto has_errors() const noexcept -> bool;
    auto values() const noexcept -> std::span<const Diagnostic>;
    auto append(DiagnosticSink&& other) noexcept -> void;
    auto take() noexcept -> Diagnostics;

private:
    Diagnostics diagnostics;
};
