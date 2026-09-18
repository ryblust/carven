module carven:backend.preparation.body;

import :backend.preparation;
import :semantic.semir.body;
import :semantic.semir.program;
import :semantic.semir.structured;
import std;

// ReadBorrow observes an object; AddressValue reads a pointer slot. Neither
// requires a C++ local declaration. Realization chooses storage at boundaries.
// ProjectionPlace forwards the consumer access through a field or element projection.
// WritePlace requires mutable access; merely locating an object does not.
// NativeTake additionally preserves the C++ query contract T&&, including for
// trivial values whose Carven transfer policy otherwise observes const T&.
enum class PreparedUse {
    ReadBorrow,
    AddressValue,
    OperandValue,
    ProjectionPlace,
    WritePlace,
    ConstPlace,
    Consume,
    NativeTake
};

enum class PreparedDemand { Effects, Value };

struct PreparedOperand final {
    const SemanticExpression* expression;
    PreparedUse use;
    PreparedDemand demand;
};

struct PreparedOperation final {
    const SemanticExpression& operation;
    bool executes_operation;
    bool requires_execution;
    bool reads_storage;
    std::vector<PreparedOperand> operands;
    std::unique_ptr<OperationPreparation> preparation;
};

class BodyPreparation final {
public:
    BodyPreparation(const SemIRProgram& semantic, BodyID body) noexcept;
    auto body() const noexcept -> const SemIRBody&;
    // Propagation markers select their operand; all other operations retain identity.
    auto operation(const SemanticExpression& source) const noexcept -> const PreparedOperation&;

private:
    auto operands(const SemanticExpression& source) const noexcept -> std::vector<PreparedOperand>;
    auto operand(const SemanticExpression& source, PreparedUse use) const noexcept
        -> PreparedOperand;
    auto argument(const SemCallArgument& source) const noexcept -> PreparedOperand;
    const SemIRProgram& semantic;
    const SemIRBody& metadata;
    std::unordered_map<const SemanticExpression*, PreparedOperation> operations;
};
