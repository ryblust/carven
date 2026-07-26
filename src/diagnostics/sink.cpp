module carven:diagnostics.sink.impl;

import :diagnostics.sink;
import std;

auto DiagnosticSink::emit(Diagnostic diagnostic) noexcept -> void {
    diagnostics.push_back(std::move(diagnostic));
}

auto DiagnosticSink::emit(std::span<const Diagnostic> diagnostics) noexcept -> void {
    this->diagnostics.insert(this->diagnostics.end(), diagnostics.begin(), diagnostics.end());
}

auto DiagnosticSink::empty() const noexcept -> bool {
    return diagnostics.empty();
}
auto DiagnosticSink::size() const noexcept -> std::size_t {
    return diagnostics.size();
}

auto DiagnosticSink::has_errors() const noexcept -> bool {
    return std::ranges::any_of(
        diagnostics,
        [](const Diagnostic& diagnostic) static noexcept -> bool {
            return diagnostic.finding.severity == DiagnosticSeverity::Error;
        }
    );
}

auto DiagnosticSink::values() const noexcept -> std::span<const Diagnostic> {
    return diagnostics;
}

auto DiagnosticSink::append(DiagnosticSink&& other) noexcept -> void {
    auto moved = other.take();
    diagnostics.insert(
        diagnostics.end(),
        std::make_move_iterator(moved.begin()),
        std::make_move_iterator(moved.end())
    );
}

auto DiagnosticSink::take() noexcept -> Diagnostics {
    return std::move(diagnostics);
}
