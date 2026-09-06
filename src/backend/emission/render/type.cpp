module carven:backend.emission.render.type.impl;

import :backend.emission.render;
import :backend.target.symbol;
import :support.visit;
import std;

namespace {
auto query_expression(const TargetTypeQuery& query) noexcept -> TargetExpr {
    auto children = std::vector<TargetExpr>();
    for (const auto& operand : query.operands) {
        children.push_back(query_expression(operand));
    }
    return std::visit(
        Overloaded {
            [&](TargetTypeID type) noexcept -> TargetExpr {
                return {
                    .value = TargetCallExpr {
                        .callee = UniqueIndirect(
                            TargetExpr {
                                .value =
                                    TargetNameExpr {
                                        .name = TargetName::globally_qualified(
                                            {TargetIdentifier::from_spelling("std"),
                                             TargetIdentifier::from_spelling("declval")}
                                        )
                                    }
                            }
                        ),
                        .template_argument_type_ids = {type},
                        .arguments = {}
                    }
                };
            },
            [&](const TargetName& name) noexcept -> TargetExpr {
                return {.value = TargetNameExpr {.name = name}};
            },
            [&](const TargetQueryCall&) noexcept -> TargetExpr {
                auto callee = std::move(children.front());
                children.erase(children.begin());
                return {
                    .value = TargetCallExpr {
                        .callee = UniqueIndirect(std::move(callee)),
                        .template_argument_type_ids = {},
                        .arguments = std::move(children)
                    }
                };
            },
            [&](const TargetQueryIndex&) noexcept -> TargetExpr {
                return {
                    .value = TargetIndexExpr {
                        .operand = UniqueIndirect(std::move(children[0])),
                        .index = UniqueIndirect(std::move(children[1]))
                    }
                };
            },
            [&](const TargetQueryMember& member) noexcept -> TargetExpr {
                return {
                    .value = TargetMemberExpr {
                        .operand = UniqueIndirect(std::move(children[0])),
                        .name = member.name
                    }
                };
            },
            [&](TargetPrefixOperator op) noexcept -> TargetExpr {
                return {
                    .value = TargetPrefixExpr {
                        .op = op,
                        .operand = UniqueIndirect(std::move(children[0]))
                    }
                };
            },
            [&](TargetBinaryOperator op) noexcept -> TargetExpr {
                return {
                    .value = TargetBinaryExpr {
                        .left = UniqueIndirect(std::move(children[0])),
                        .op = op,
                        .right = UniqueIndirect(std::move(children[1]))
                    }
                };
            },
        },
        query.operation
    );
}
} // namespace

auto TargetRenderer::render_type(TargetTypeID id) noexcept -> LayoutNodeID {
    return render_type_layouts(id).wrapping;
}

auto TargetRenderer::render_type_layouts(TargetTypeID id) noexcept -> SyntaxLayouts {
    const auto& value = unit.type(id);
    auto rendered = std::visit(
        Overloaded {
            [&](const TargetDeducedType& deduced) noexcept -> SyntaxLayouts {
                const auto expression = query_expression(deduced.query);
                const auto result = concat(
                    {text("std::remove_cvref_t<decltype("),
                     render_expression(expression),
                     text(")>")}
                );
                return {.inline_qualified = result, .wrapping = result};
            },
            [&](const TargetNamedType& named) noexcept -> SyntaxLayouts {
                const auto segment =
                    [&](SyntaxLayouts segment_name,
                        std::span<const TargetTypeID> type_argument_ids) noexcept -> SyntaxLayouts {
                    if (type_argument_ids.empty()) {
                        return segment_name;
                    }
                    auto argument_layout_ids = std::vector<LayoutNodeID> {};
                    argument_layout_ids.reserve(type_argument_ids.size());
                    for (const auto argument_type_id : type_argument_ids) {
                        argument_layout_ids.push_back(render_type(argument_type_id));
                    }
                    const auto arguments_layout = delimited_list(argument_layout_ids, "<", ">");
                    return {
                        .inline_qualified =
                            concat({segment_name.inline_qualified, arguments_layout}),
                        .wrapping = concat({segment_name.wrapping, arguments_layout}),
                    };
                };
                const auto base_name = render_name_layouts(named.name);
                auto parts = std::vector<SyntaxLayouts> {
                    segment(base_name, named.type_argument_ids),
                };
                for (const auto& nested : named.nested) {
                    const auto name = render_identifier(nested.name);
                    parts.push_back(segment(
                        {.inline_qualified = name, .wrapping = name},
                        nested.type_argument_ids
                    ));
                }
                return qualified_sequence(parts, false);
            },
            [&](const TargetIntrinsicType& intrinsic) noexcept -> SyntaxLayouts {
                auto result = text(target_symbol_spelling(intrinsic.symbol));
                if (!intrinsic.type_argument_ids.empty()) {
                    auto argument_layout_ids = std::vector<LayoutNodeID> {};
                    argument_layout_ids.reserve(intrinsic.type_argument_ids.size());
                    for (const auto argument_type_id : intrinsic.type_argument_ids) {
                        argument_layout_ids.push_back(render_type(argument_type_id));
                    }
                    result = concat({result, delimited_list(argument_layout_ids, "<", ">")});
                }
                return {.inline_qualified = result, .wrapping = result};
            },
            [&](const TargetArrayType& array) noexcept -> SyntaxLayouts {
                const auto arguments = std::array {
                    render_type(array.element_type_id),
                    text(std::to_string(array.extent.magnitude))
                };
                const auto result =
                    concat({text("std::array"), delimited_list(arguments, "<", ">")});
                return {.inline_qualified = result, .wrapping = result};
            },
            [&](const TargetFunctionType& function) noexcept -> SyntaxLayouts {
                const auto result = render_type(function.result);
                auto parameters = std::vector<LayoutNodeID> {};
                for (const auto parameter : function.parameters) {
                    parameters.push_back(render_type(parameter));
                }
                const auto signature =
                    concat({result, delimited_list(parameters, "(", ")"), text(" noexcept")});
                const auto arguments = std::array {signature};
                const auto function_ref = concat(
                    {text(target_symbol_spelling(TargetSymbol::RuntimeFunctionRef)),
                     delimited_list(arguments, "<", ">")}
                );
                return {.inline_qualified = function_ref, .wrapping = function_ref};
            },
            [&](const TargetPointerType& pointer) noexcept -> SyntaxLayouts {
                const auto pointee = render_type_layouts(pointer.pointee);
                return {
                    .inline_qualified = concat({pointee.inline_qualified, text("*")}),
                    .wrapping = concat({pointee.wrapping, text("*")}),
                };
            },
            [&](const TargetReferenceType& reference) noexcept -> SyntaxLayouts {
                const auto referent = render_type_layouts(reference.referent);
                const auto prefix = text(reference.const_qualified ? "const " : "");
                const auto suffix = text(reference.rvalue ? "&&" : "&");
                return {
                    .inline_qualified = concat({prefix, referent.inline_qualified, suffix}),
                    .wrapping = concat({prefix, referent.wrapping, suffix}),
                };
            },
        },
        value.value
    );
    if (value.const_qualified) {
        if (std::holds_alternative<TargetPointerType>(value.value)) {
            rendered.inline_qualified = concat({rendered.inline_qualified, text(" const")});
            rendered.wrapping = concat({rendered.wrapping, text(" const")});
        } else {
            rendered.inline_qualified = concat({text("const "), rendered.inline_qualified});
            rendered.wrapping = concat({text("const "), rendered.wrapping});
        }
    }
    return rendered;
}
