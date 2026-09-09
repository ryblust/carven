module carven:semantic.analysis.diagnostics;

import :diagnostics.diagnostic;
import :diagnostics.sink;
import std;

class AnalysisFailure final {
public:
    AnalysisFailure(const AnalysisFailure&) = default;
    AnalysisFailure(AnalysisFailure&&) = default;
    auto operator=(const AnalysisFailure&) -> AnalysisFailure& = default;
    auto operator=(AnalysisFailure&&) -> AnalysisFailure& = default;
    ~AnalysisFailure() = default;

private:
    constexpr AnalysisFailure() noexcept = default;

    friend class AnalysisDiagnostics;
};

class AnalysisDiagnostics final {
public:
    explicit AnalysisDiagnostics(DiagnosticSink& sink) noexcept;

    auto error(Diagnostic diagnostic) const noexcept -> AnalysisFailure;
    auto warning(Diagnostic diagnostic) const noexcept -> void;
    auto has_errors() const noexcept -> bool;

    auto failure() const noexcept -> std::optional<AnalysisFailure> {
        return has_errors() ? std::optional(AnalysisFailure {}) : std::nullopt;
    }

private:
    DiagnosticSink* diagnostic_sink;
};

template<typename Value>
using AnalysisResult = std::expected<Value, AnalysisFailure>;
