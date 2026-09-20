module carven:backend.preparation;

import :backend.preparation.arithmetic;
import :backend.preparation.format;
import :semantic.semir.callable;
import :semantic.semir.program;
import :semantic.semir.structured;
import std;

struct PreparedPrint final {
    std::vector<std::optional<std::string>> operand_text;
};

struct PreparedCallableAdaptation final {
    CallableAdaptation adaptation;
    bool array;
};

// Each entry supplies a retained value index or borrows a constant argument
// from the semantic construction query. Entries remain in source argument order.
using PreparedNativeArgument = std::variant<std::size_t, const CppConstructArgument*>;

struct PreparedNativeConstruction final {
    std::vector<PreparedNativeArgument> arguments;
};

using OperationPreparation = std::variant<
    PreparedFormat,
    PreparedPrint,
    PreparedCallableAdaptation,
    PreparedUnary,
    PreparedBinary,
    PreparedNativeConstruction>;

// Plans own generated data and borrow semantic arguments from the program,
// which must outlive preparation and realization.
auto prepare_operation(const SemIRProgram& program, const SemanticExpression& source) noexcept
    -> std::unique_ptr<OperationPreparation>;
