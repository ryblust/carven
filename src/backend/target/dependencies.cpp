module carven:backend.target.dependencies.impl;

import :backend.target.dependencies;
import :backend.target.symbol;
import :backend.target.traversal;
import :support.invariant;
import :support.visit;
import std;

namespace {

class TargetDependencyCollector final {
public:
    TargetDependencyCollector(
        TargetUnitIdentity unit_identity,
        std::span<const TargetType> source_types,
        const TargetUnitSections& source_sections
    ) noexcept;
    auto collect() noexcept -> std::vector<TargetDirective>;
    auto visit_type(TargetTypeID id) noexcept -> bool;
    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
    auto enter_statement(const TargetStmt& statement) noexcept -> bool;

private:
    auto include(std::string_view header) noexcept -> void;
    auto visit_symbol(TargetSymbol symbol) noexcept -> void;

    TargetUnitIdentity identity;
    std::span<const TargetType> types;
    const TargetUnitSections& sections;
    std::vector<bool> visited_types;
    std::vector<TargetTypeID> pending_types;
    std::flat_set<std::string> headers;
};

TargetDependencyCollector::TargetDependencyCollector(
    TargetUnitIdentity unit_identity,
    std::span<const TargetType> source_types,
    const TargetUnitSections& source_sections
) noexcept
    : identity(unit_identity),
      types(source_types),
      sections(source_sections),
      visited_types(source_types.size(), false) {}

auto TargetDependencyCollector::collect() noexcept -> std::vector<TargetDirective> {
    if (!traverse_target_unit(sections, *this)) {
        invariant_violation("target dependency traversal failed");
    }
    while (!pending_types.empty()) {
        const auto id = pending_types.back();
        pending_types.pop_back();
        const auto& value = types[id.index()].value;
        value.visit(
            Overloaded {
                [](const TargetDecltypeType&) static noexcept {},
                [](const TargetNamedType&) static noexcept {},
                [&](const TargetIntrinsicType& intrinsic) noexcept {
                    visit_symbol(intrinsic.symbol);
                },
                [&](const TargetArrayType&) noexcept { include("array"); },
                [&](const TargetFunctionType&) noexcept {
                    visit_symbol(TargetSymbol::RuntimeFunctionRef);
                },
                [](const TargetPointerType&) static noexcept {},
                [](const TargetReferenceType&) static noexcept {},
            }
        );
        if (!visit_target_type_children(value, *this)) {
            invariant_violation("target type dependency traversal failed");
        }
    }
    return headers | std::views::transform([](const std::string& header) static noexcept {
               return TargetDirective {.bytes = std::format("#include <{}>", header)};
           })
        | std::ranges::to<std::vector>();
}

auto TargetDependencyCollector::visit_type(TargetTypeID id) noexcept -> bool {
    if (id.owner() != identity || id.index() >= types.size()) {
        invariant_violation("target dependency traversal reached an invalid type ID");
    }
    if (visited_types[id.index()]) {
        return true;
    }
    visited_types[id.index()] = true;
    pending_types.push_back(id);
    return true;
}

auto TargetDependencyCollector::enter_expression(
    const TargetExpr& expression,
    TargetExpressionRole
) noexcept -> bool {
    expression.value.visit(
        Overloaded {
            [](const TargetNameExpr&) static noexcept {},
            [](const TargetLocalExpr&) static noexcept {},
            [&](const TargetIntrinsicNameExpr& intrinsic) noexcept {
                visit_symbol(intrinsic.symbol);
            },
            [&](const TargetLiteralExpr& literal) noexcept {
                const auto* string = std::get_if<TargetStringLiteral>(&literal.value);
                if (string != nullptr && string->kind == TargetStringLiteralKind::StringView) {
                    include("string_view");
                }
            },
            [](const TargetPrefixExpr&) static noexcept {},
            [](const TargetBinaryExpr&) static noexcept {},
            [](const TargetConditionalExpr&) static noexcept {},
            [](const TargetCallExpr&) static noexcept {},
            [&](const TargetArrayExpr&) noexcept { include("array"); },
            [](const TargetConstructionExpr&) static noexcept {},
            [](const TargetIndexExpr&) static noexcept {},
            [](const TargetMemberExpr&) static noexcept {},
            [](const TargetScopeMemberExpr&) static noexcept {},
            [](const TargetStaticMemberExpr&) static noexcept {},
            [](const TargetStaticCastExpr&) static noexcept {},
            [](const TargetLambdaExpr&) static noexcept {},
        }
    );
    return true;
}

auto TargetDependencyCollector::enter_statement(const TargetStmt& statement) noexcept -> bool {
    if (std::holds_alternative<TargetUnreachableStmt>(statement.value)) {
        include("carven/runtime/unreachable.hpp");
    }
    if (std::holds_alternative<TargetRuntimeTrapStmt>(statement.value)) {
        include("cstdlib");
    }
    return true;
}

auto TargetDependencyCollector::include(std::string_view header) noexcept -> void {
    headers.insert(std::string(header));
}

auto TargetDependencyCollector::visit_symbol(TargetSymbol symbol) noexcept -> void {
    if (const auto header = target_symbol_info(symbol).header; !header.empty()) {
        include(header);
    }
}

} // namespace

auto collect_target_dependencies(
    TargetUnitIdentity identity,
    std::span<const TargetType> types,
    const TargetUnitSections& sections
) noexcept -> std::vector<TargetDirective> {
    return TargetDependencyCollector(identity, types, sections).collect();
}
