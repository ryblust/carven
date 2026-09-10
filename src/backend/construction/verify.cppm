module carven:backend.construction.verify;

import :backend.construction;
import :semantic.semir;
import std;

enum class ConstructionViolationKind {
    InvalidIdentity,
    DuplicateExecution,
    ExecutionCycle,
    UnownedExecution,
    InvalidControl,
    InvalidLifetime,
};

struct ConstructionViolation final {
    ConstructionViolationKind kind;
    std::optional<ProgramOriginID> origin;
    std::string_view message;
};

// Checks only relationships introduced by construction. Published semantic
// metadata remains responsible for types, ownership and lifetime analysis.
auto validate_construction(const BodyConstruction& body, const SemIRBody& metadata) noexcept
    -> std::expected<void, ConstructionViolation>;
