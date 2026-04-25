export module zero.common.error;

import zero.common.source;
import std;

export enum class Severity : std::uint8_t {
    Error,
    Warning,
    Note,
};

export constexpr auto display_severity(Severity severity) noexcept -> const char* {
    switch (severity) {
        using enum Severity;
        case Error:   return "error";
        case Warning: return "warning";
        case Note:    return "note";
    }
    return "unknown";
}

export struct Diagnostic final {
    Severity severity;
    std::string message;
    Span span;
};

export class DiagnosticEngine final {
    std::vector<Diagnostic> diagnostics;

public:
    constexpr DiagnosticEngine() noexcept = default;

    constexpr auto push(Severity severity, std::string message, Span span) noexcept -> void {
        diagnostics.emplace_back(severity, std::move(message), span);
    }

    constexpr auto error_count() const noexcept -> std::size_t {
        return std::ranges::count_if(diagnostics, [](const auto& d) {
            return d.severity == Severity::Error;
        });
    }

    constexpr auto warning_count() const noexcept -> std::size_t {
        return std::ranges::count_if(diagnostics, [](const auto& d) {
            return d.severity == Severity::Warning;
        });
    }

    constexpr auto has_errors() const noexcept -> bool {
        return error_count() > 0;
    }

    constexpr auto items() const noexcept -> std::span<const Diagnostic> {
        return diagnostics;
    }

    constexpr auto empty() const noexcept -> bool {
        return diagnostics.empty();
    }

    auto render() const noexcept -> std::string {
        auto result = std::string{};
        for (const auto& d : diagnostics) {
            result += display_severity(d.severity);
            result += ": ";
            result += d.message;
            result += '\n';
        }
        return result;
    }
};

template<> struct std::formatter<Severity> final {
    constexpr auto parse(const auto& context) const noexcept {
        return context.begin();
    }

    constexpr auto format(Severity severity, auto&& context) const noexcept {
        return std::format_to(context.out(), "{}", display_severity(severity));
    }
};

template<> struct std::formatter<Diagnostic> final {
    constexpr auto parse(const auto& context) const noexcept {
        return context.begin();
    }

    constexpr auto format(const Diagnostic& diagnostic, auto&& context) const noexcept {
        return std::format_to(context.out(), "{}: {}",
            diagnostic.severity, diagnostic.message);
    }
};
