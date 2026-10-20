module carven:semantic.analysis.catalog.builder;

import :diagnostics.diagnostic;
import :frontend.program;
import :semantic.analysis.catalog;
import :semantic.analysis.program;
import :source.text;
import std;

class AnalysisCatalogBuilder final {
public:
    explicit AnalysisCatalogBuilder(
        ProgramDraft& draft,
        CheckedDefinitions* seed = nullptr
    ) noexcept;

    auto build() && noexcept -> std::expected<AnalysisCatalog, Diagnostics>;

private:
    auto collect_declarations() noexcept -> void;
    auto bind_imports() noexcept -> void;

    auto bind_cpp_imports(
        ProgramModuleID module_id,
        const std::flat_map<std::string, Span, std::less<>>& declarations
    ) noexcept -> void;

    auto provide_defaults() noexcept -> void;

    static auto binding_error(SourceID source, std::string message, Span span) noexcept
        -> Diagnostic;

    ProgramDraft& draft;
    AnalysisCatalog result;
    Diagnostics diagnostics;
};
