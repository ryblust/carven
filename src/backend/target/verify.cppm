module carven:backend.target.verify;

import :backend.target.expr;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.type;
import :backend.target.unit;
import std;

enum class TargetUnitViolationKind {
    InvalidReference,
    InvalidOwnership,
    InvalidCycle,
    InvalidAttribution,
    InvalidStructure,
    InvalidControl,
    OrphanNode,
};

struct TargetUnitViolation final {
    TargetUnitViolationKind kind;
    std::string message;
};

struct TargetUnitValidationView final {
    std::span<const TargetType> types;
    std::span<const TargetExpr> expressions;
    std::span<const TargetStmt> statements;
    std::span<const TargetItem> items;
    const TargetUnitRoot& root;
};

auto validate_target_unit(TargetUnitValidationView unit) noexcept
    -> std::expected<void, TargetUnitViolation>;
