module carven:backend.emission.render.type.impl;

import :backend.emission.render;
import :backend.target.symbol;
import :support.visit;
import std;

auto TargetRenderer::render_template_argument(TargetTemplateArgument value) noexcept
    -> LayoutNodeID {
    return std::visit(
        Overloaded {
            [&](TargetTypeID id) noexcept { return render_type(id); },
            [&](TargetExprID id) noexcept { return render_expression(id); },
        },
        value
    );
}

auto TargetRenderer::render_type(TargetTypeID id) noexcept -> LayoutNodeID {
    return render_type_layouts(id).wrapping;
}

auto TargetRenderer::render_type_layouts(TargetTypeID id) noexcept -> SyntaxLayouts {
    const auto& value = unit.type(id);
    auto rendered = std::visit(
        Overloaded {
            [&](const TargetNamedType& named) noexcept -> SyntaxLayouts {
                const auto segment = [&](
                                         SyntaxLayouts segment_name,
                                         std::span<const TargetTemplateArgument> arguments
                                     ) noexcept -> SyntaxLayouts {
                    if (arguments.empty()) {
                        return segment_name;
                    }
                    auto values = std::vector<LayoutNodeID> {};
                    values.reserve(arguments.size());
                    for (const auto argument : arguments) {
                        values.push_back(render_template_argument(argument));
                    }
                    const auto arguments_layout = delimited_list(values, "<", ">");
                    return {
                        .inline_qualified =
                            concat({segment_name.inline_qualified, arguments_layout}),
                        .wrapping = concat({segment_name.wrapping, arguments_layout}),
                    };
                };
                const auto base_name = render_name_layouts(named.name);
                auto parts = std::vector<SyntaxLayouts> {
                    segment(base_name, named.arguments),
                };
                for (const auto& nested : named.nested) {
                    const auto name = render_identifier(nested.name);
                    parts.push_back(
                        segment({.inline_qualified = name, .wrapping = name}, nested.arguments)
                    );
                }
                return qualified_sequence(parts, false);
            },
            [&](const TargetIntrinsicType& intrinsic) noexcept -> SyntaxLayouts {
                auto result = text(target_symbol_spelling(intrinsic.symbol));
                if (!intrinsic.arguments.empty()) {
                    auto arguments = std::vector<LayoutNodeID> {};
                    arguments.reserve(intrinsic.arguments.size());
                    for (const auto argument : intrinsic.arguments) {
                        arguments.push_back(render_template_argument(argument));
                    }
                    result = concat({result, delimited_list(arguments, "<", ">")});
                }
                return {.inline_qualified = result, .wrapping = result};
            },
            [&](const TargetArrayType& array) noexcept -> SyntaxLayouts {
                const auto arguments = std::array {
                    render_type(array.element_type_id),
                    render_expression(array.extent)
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
