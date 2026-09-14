module carven:backend.preparation;

import :backend.preparation.format;
import :semantic.semir.program;
import :semantic.semir.structured;
import std;

struct PreparedPrint final {
    std::vector<std::optional<std::string>> operand_text;
};

using OperationPreparation = std::variant<PreparedFormat, PreparedPrint>;

// Prepared bytes are owned here; semantic constants remain immutable.
auto prepare_operation(const SemIRProgram& program, const SemanticExpression& source) noexcept
    -> std::unique_ptr<OperationPreparation>;
