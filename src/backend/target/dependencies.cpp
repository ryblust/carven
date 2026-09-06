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
    ) noexcept
        : identity(unit_identity),
          types(source_types),
          sections(source_sections),
          visited_types(source_types.size(), false) {}

    auto collect() noexcept -> std::vector<TargetDirective> {
        if (!traverse_target_unit(sections, *this)) {
            invariant_violation("target dependency traversal failed");
        }
        return headers | std::views::transform([](const std::string& header) noexcept {
                   return TargetDirective {.bytes = std::format("#include <{}>", header)};
               })
            | std::ranges::to<std::vector>();
    }

    auto visit_type(TargetTypeID id) noexcept -> bool {
        if (id.owner() != identity || id.index() >= types.size()) {
            invariant_violation("target dependency traversal reached an invalid type ID");
        }
        if (visited_types[id.index()]) {
            return true;
        }
        visited_types[id.index()] = true;
        const auto& value = types[id.index()].value;
        std::visit(
            Overloaded {
                [&](const TargetDeducedType&) noexcept {
                    include("type_traits");
                    include("utility");
                },
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
            },
            value
        );
        return visit_target_type_children(value, *this);
    }

    auto enter_expression(const TargetExpr& expression) noexcept -> bool {
        std::visit(
            Overloaded {
                [](const TargetNameExpr&) static noexcept {},
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
                [](const TargetRegionExpr&) static noexcept {},
            },
            expression.value
        );
        return true;
    }

    auto enter_statement(const TargetStmt& statement) noexcept -> bool {
        if (std::holds_alternative<TargetUnreachableStmt>(statement.value)) {
            include("carven/runtime/unreachable.hpp");
        }
        if (std::holds_alternative<TargetRuntimeTrapStmt>(statement.value)) {
            include("cstdlib");
        }
        return true;
    }

private:
    auto include(std::string_view header) noexcept -> void { headers.insert(std::string(header)); }

    auto visit_symbol(TargetSymbol symbol) noexcept -> void {
        switch (symbol) {
            case TargetSymbol::StdInt8:
            case TargetSymbol::StdInt16:
            case TargetSymbol::StdInt32:
            case TargetSymbol::StdInt64:
            case TargetSymbol::StdUInt8:
            case TargetSymbol::StdUInt16:
            case TargetSymbol::StdUInt32:
            case TargetSymbol::StdUInt64:           include("cstdint"); break;
            case TargetSymbol::StdPtrdiff:
            case TargetSymbol::StdSize:             include("cstddef"); break;
            case TargetSymbol::RuntimeOutcome:      include("carven/runtime/outcome.hpp"); break;
            case TargetSymbol::RuntimeStrBytesView:
            case TargetSymbol::RuntimeStrCharsView:
            case TargetSymbol::RuntimeStrBytes:
            case TargetSymbol::RuntimeStrChars:
            case TargetSymbol::RuntimeCheckedUnicodeScalar:
                include("carven/runtime/text.hpp");
                break;
            case TargetSymbol::RuntimeEntryArgs:     include("carven/runtime/entry.hpp"); break;
            case TargetSymbol::RuntimeReadArg:
            case TargetSymbol::RuntimeTransfer:      include("carven/runtime/passing.hpp"); break;
            case TargetSymbol::RuntimeFunctionRef:   include("carven/runtime/callable.hpp"); break;
            case TargetSymbol::RuntimeIntegerNegate:
            case TargetSymbol::RuntimeIntegerAdd:
            case TargetSymbol::RuntimeIntegerSubtract:
            case TargetSymbol::RuntimeIntegerMultiply:
            case TargetSymbol::RuntimeIntegerDivide:
            case TargetSymbol::RuntimeIntegerRemainder:
            case TargetSymbol::RuntimeIntegerLeftShift:
            case TargetSymbol::RuntimeIntegerRightShift:
            case TargetSymbol::RuntimeIntegerAddAssign:
            case TargetSymbol::RuntimeIntegerSubtractAssign:
            case TargetSymbol::RuntimeIntegerMultiplyAssign:
            case TargetSymbol::RuntimeIntegerDivideAssign:
            case TargetSymbol::RuntimeIntegerRemainderAssign:
            case TargetSymbol::RuntimeIntegerLeftShiftAssign:
            case TargetSymbol::RuntimeIntegerRightShiftAssign:
            case TargetSymbol::RuntimeIntegerIncrement:
            case TargetSymbol::RuntimeIntegerDecrement:
                include("carven/runtime/numeric.hpp");
                break;
            case TargetSymbol::RuntimeCheckedArrayIndex: include("carven/runtime/array.hpp"); break;
            case TargetSymbol::StdReferenceWrapper:      include("functional"); break;
            case TargetSymbol::StdAddressof:             include("memory"); break;
            case TargetSymbol::StdGetIf:
            case TargetSymbol::StdVariant:               include("variant"); break;
            case TargetSymbol::StdForward:
            case TargetSymbol::StdMove:                  include("utility"); break;
            case TargetSymbol::StdNullopt:
            case TargetSymbol::StdOptional:              include("optional"); break;
            case TargetSymbol::StdStringView:            include("string_view"); break;
            case TargetSymbol::TestingContext:
            case TargetSymbol::TestingReporter:
            case TargetSymbol::TestingControl: include("carven/std/testing/testing.hpp"); break;
            case TargetSymbol::Auto:
            case TargetSymbol::Void:
            case TargetSymbol::Bool:
            case TargetSymbol::Char:
            case TargetSymbol::Int:
            case TargetSymbol::CChar:
            case TargetSymbol::Float:
            case TargetSymbol::Double:
            case TargetSymbol::StdNullptr:     break;
        }
    }

    TargetUnitIdentity identity;
    std::span<const TargetType> types;
    const TargetUnitSections& sections;
    std::vector<bool> visited_types;
    std::flat_set<std::string> headers;
};

} // namespace

auto collect_target_dependencies(
    TargetUnitIdentity identity,
    std::span<const TargetType> types,
    const TargetUnitSections& sections
) noexcept -> std::vector<TargetDirective> {
    return TargetDependencyCollector(identity, types, sections).collect();
}
