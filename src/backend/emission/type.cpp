module carven:backend.emission.type.impl;

import :backend.emission.render;
import :backend.target.symbol;
import :support.visit;
import std;

auto TargetRenderer::render_type(TargetTypeID id, bool constant) noexcept -> LayoutNodeID {
    return type_layouts.at(id.index()).value()[constant ? 1 : 0];
}

auto TargetRenderer::render_type_node(TargetTypeID id, bool constant) noexcept -> LayoutNodeID {
    const auto& value = unit.type(id);
    auto rendered = value.value.visit(
        Overloaded {
            [&](const TargetDecltypeType& deduced) noexcept {
                return concat(
                    {text("decltype(("),
                     render_expression(deduced.expression(), TargetPrecedence::Lowest),
                     text("))")}
                );
            },
            [&](const TargetNamedType& named) noexcept {
                const auto segment =
                    [&](LayoutNodeID segment_name,
                        std::span<const TargetTypeID> type_argument_ids) noexcept -> LayoutNodeID {
                    if (type_argument_ids.empty()) {
                        return segment_name;
                    }
                    auto argument_layout_ids = std::vector<LayoutNodeID> {};
                    argument_layout_ids.reserve(type_argument_ids.size());
                    for (const auto argument_type_id : type_argument_ids) {
                        argument_layout_ids.push_back(render_type(argument_type_id, false));
                    }
                    const auto arguments_layout = delimited_list(argument_layout_ids, "<", ">");
                    return concat({segment_name, arguments_layout});
                };
                const auto base_name = render_name(named.name);
                auto parts = std::vector<LayoutNodeID> {
                    segment(base_name, named.type_argument_ids),
                };
                for (const auto& nested : named.nested) {
                    parts.push_back(
                        segment(render_identifier(nested.name), nested.type_argument_ids)
                    );
                }
                return qualified_sequence(parts, false);
            },
            [&](const TargetIntrinsicType& intrinsic) noexcept {
                auto result = text(target_symbol_info(intrinsic.native_symbol()).spelling);
                if (!intrinsic.type_argument_ids.empty()) {
                    auto argument_layout_ids = std::vector<LayoutNodeID> {};
                    argument_layout_ids.reserve(intrinsic.type_argument_ids.size());
                    for (const auto argument_type_id : intrinsic.type_argument_ids) {
                        argument_layout_ids.push_back(render_type(argument_type_id, false));
                    }
                    result = concat({result, delimited_list(argument_layout_ids, "<", ">")});
                }
                return result;
            },
            [&](const TargetArrayType& array) noexcept {
                const auto arguments = std::array {
                    render_type(array.element_type_id, false),
                    text(std::to_string(array.extent.magnitude))
                };
                return concat(
                    {text(target_symbol_info(array.native_symbol()).spelling),
                     delimited_list(arguments, "<", ">")}
                );
            },
            [&](const TargetFunctionRefType& function_ref) noexcept {
                const auto result = render_type(function_ref.result, false);
                auto parameters = std::vector<LayoutNodeID> {};
                for (const auto parameter : function_ref.parameters) {
                    parameters.push_back(render_type(parameter, false));
                }
                const auto signature =
                    concat({result, delimited_list(parameters, "(", ")"), text(" noexcept")});
                const auto arguments = std::array {signature};
                return concat(
                    {text(target_symbol_info(function_ref.native_symbol()).spelling),
                     delimited_list(arguments, "<", ">")}
                );
            },
            [&](const TargetPointerType& pointer) noexcept {
                return concat({render_type(pointer.pointee, false), text("*")});
            },
            [&](const TargetReferenceType& reference) noexcept {
                const auto referent = render_type(reference.referent, reference.const_qualified);
                const auto suffix = text(reference.rvalue ? "&&" : "&");
                return concat({referent, suffix});
            },
        }
    );
    if (value.const_qualified || constant) {
        if (std::holds_alternative<TargetPointerType>(value.value)) {
            rendered = concat({rendered, text(" const")});
        } else {
            rendered = concat({text("const "), rendered});
        }
    }
    return rendered;
}
