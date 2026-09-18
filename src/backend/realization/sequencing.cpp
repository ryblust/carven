module carven:backend.realization.sequencing.impl;

import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.preparation.body;
import :backend.realization.expr;
import :backend.realization.operation;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.stmt;
import :semantic.semir.delegation;
import :semantic.semir.ids;
import :semantic.semir.structured;
import std;

auto BodyRealizer::ExpressionBuilder::discard_pending(Fragment& fragment) noexcept -> void {
    if (!pending(fragment)) {
        return;
    }
    if (fragment.executes) {
        fragment.statements.emit(
            discarded_operation(owner.context, source(fragment).operation, raw(fragment))
        );
    }
    complete(fragment, LoweringCompleted {});
}

auto BodyRealizer::ExpressionBuilder::has_storage_read(const Fragment& fragment) const noexcept
    -> bool {
    return pending(fragment) && fragment.observes;
}

auto BodyRealizer::ExpressionBuilder::has_effect(const Fragment& fragment) const noexcept -> bool {
    return pending(fragment) && fragment.executes;
}

auto BodyRealizer::ExpressionBuilder::sequenced_suffix_begin(
    const PreparedOperation& value
) const noexcept -> std::size_t {
    if (const auto* structure = std::get_if<SemStruct>(&value.operation.value)) {
        // Pure values impose no ordering constraint. Preserve declaration order
        // among effects and storage reads so the suffix can initialize in place.
        auto next = std::numeric_limits<std::uint32_t>::max();
        for (auto index = structure->fields.size(); index != 0uz;) {
            --index;
            const auto& operand = owner.preparation.summary(*value.operands[index].expression);
            if (!operand.requires_execution && !operand.reads_storage) {
                continue;
            }
            const auto field = structure->fields[index].declaration_index;
            if (field > next) {
                return index + 1uz;
            }
            next = field;
        }
        return 0uz;
    }
    if (std::holds_alternative<SemArray>(value.operation.value)
        || std::holds_alternative<SemClosure>(value.operation.value)) {
        return 0uz;
    }
    return value.operands.size();
}

auto BodyRealizer::ExpressionBuilder::first_unsequenced(
    const PreparedOperation& value
) const noexcept -> std::size_t {
    if (std::holds_alternative<SemCall>(value.operation.value)) {
        return 1uz;
    }
    if (const auto* native = std::get_if<SemCppCall>(&value.operation.value)) {
        return std::holds_alternative<CppNameReference>(native->callee) ? 0uz : 1uz;
    }
    return 0uz;
}
