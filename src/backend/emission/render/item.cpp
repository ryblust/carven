module carven:backend.emission.render.item.impl;

import :backend.emission.render;
import :backend.emission.render.string;
import :backend.target.raw;
import :backend.target.symbol;
import :support.visit;
import std;

auto TargetRenderer::render_member_function_name(const TargetMemberFunctionName& value) noexcept
    -> LayoutNodeID {
    return std::visit(
        Overloaded {
            [&](const TargetIdentifier& name) noexcept { return render_identifier(name); },
            [&](TargetOperatorName name) noexcept {
                switch (name) {
                    case TargetOperatorName::Assignment: return text("operator=");
                    case TargetOperatorName::Equality:   return text("operator==");
                }
                std::unreachable();
            },
        },
        value
    );
}

auto TargetRenderer::render_parameter(const TargetParameter& value) noexcept -> LayoutNodeID {
    const auto prefix = text(value.maybe_unused ? "[[maybe_unused]] " : "");
    if (!value.name.has_value()) {
        return concat({prefix, render_type(value.type)});
    }
    return concat({prefix, render_type(value.type), text(" "), render_identifier(*value.name)});
}

auto TargetRenderer::render_trailing_return(SyntaxLayouts result, bool const_qualified) noexcept
    -> LayoutNodeID {
    const auto qualifiers = text(const_qualified ? " const noexcept" : " noexcept");
    return choice(
        {concat({qualifiers, text(" -> "), result.inline_qualified}),
         concat(
             {qualifiers,
              builder.indent(indent_width, concat({builder.line(), text("-> "), result.wrapping}))}
         )}
    );
}

auto TargetRenderer::render_function_declarator(
    std::string_view prefix,
    SyntaxLayouts name,
    std::span<const TargetParameter> parameters,
    SyntaxLayouts result,
    bool const_qualified
) noexcept -> LayoutNodeID {
    auto rendered_parameters = std::vector<LayoutNodeID> {};
    rendered_parameters.reserve(parameters.size());
    for (const auto& parameter : parameters) {
        rendered_parameters.push_back(render_parameter(parameter));
    }
    const auto head = concat({text(prefix), text("auto "), name.inline_qualified});
    const auto wrapping_head = concat({text(prefix), text("auto "), name.wrapping});
    const auto parameter_list = delimited_list(rendered_parameters, "(", ")");
    const auto flat_parameters = builder.flatten(parameter_list);
    const auto qualifiers = text(const_qualified ? " const noexcept" : " noexcept");
    return choice(
        {concat({head, flat_parameters, qualifiers, text(" -> "), result.inline_qualified}),
         concat(
             {head,
              flat_parameters,
              qualifiers,
              builder.indent(indent_width, concat({builder.line(), text("-> "), result.wrapping}))}
         ),
         concat({head, parameter_list, render_trailing_return(result, const_qualified)}),
         concat({wrapping_head, parameter_list, render_trailing_return(result, const_qualified)})}
    );
}

auto TargetRenderer::render_record_member(const TargetRecordMember& value) noexcept
    -> LayoutNodeID {
    return std::visit(
        Overloaded {
            [&](const TargetStructField& field) noexcept {
                return concat(
                    {render_type(field.type), text(" "), render_identifier(field.name), text(";")}
                );
            },
            [&](const TargetMemberFunctionDecl& function) noexcept {
                return render_class_member(TargetClassMember {function});
            },
        },
        value
    );
}

auto TargetRenderer::render_class_member(const TargetClassMember& value) noexcept -> LayoutNodeID {
    return std::visit(
        Overloaded {
            [&](const TargetMemberVariable& member) noexcept {
                return concat(
                    {text(member.static_specifier ? "static " : ""),
                     text(member.const_specifier ? "const " : ""),
                     render_type(member.type),
                     text(" "),
                     render_identifier(member.name),
                     text(";")}
                );
            },
            [&](const TargetNestedRecord& record) noexcept {
                auto members = std::vector<LayoutNodeID> {};
                for (const auto& member : record.members) {
                    members.push_back(render_record_member(member));
                }
                return concat(
                    {text("struct "),
                     render_identifier(record.name),
                     text(" final "),
                     braced_block(members),
                     text(";")}
                );
            },
            [&](const TargetTypeAlias& alias) noexcept {
                return concat(
                    {text("using "),
                     render_identifier(alias.name),
                     text(" = "),
                     render_type(alias.type),
                     text(";")}
                );
            },
            [&](const TargetConstructorDecl& constructor) noexcept {
                auto parameters = std::vector<LayoutNodeID> {};
                for (const auto& parameter : constructor.parameters) {
                    parameters.push_back(render_parameter(parameter));
                }
                auto rendered = concat(
                    {text(constructor.constexpr_specifier ? "constexpr " : ""),
                     text(constructor.explicit_specifier ? "explicit " : ""),
                     render_identifier(constructor.name),
                     delimited_list(parameters, "(", ")"),
                     text(" noexcept")}
                );
                if (constructor.defaulted) {
                    rendered = concat({rendered, text(" = default;")});
                } else {
                    auto initializers = std::vector<LayoutNodeID> {};
                    for (const auto& initializer : constructor.initializers) {
                        initializers.push_back(concat(
                            {render_identifier(initializer.name),
                             delimited_list(
                                 std::array {render_expression(initializer.value)},
                                 "(",
                                 ")"
                             )}
                        ));
                    }
                    if (!initializers.empty()) {
                        const auto flat = builder.flatten(builder.join(initializers, text(", ")));
                        const auto broken =
                            builder.join(initializers, concat({text(","), builder.line()}));
                        rendered = choice(
                            {concat({rendered, text(" : "), flat}),
                             concat(
                                 {rendered,
                                  builder.indent(
                                      indent_width,
                                      concat({builder.line(), text(": "), broken})
                                  )}
                             )}
                        );
                    }
                    rendered = concat({rendered, text(" "), braced_block({})});
                }
                if (!constructor.template_type_parameters.empty()) {
                    auto templates = std::vector<LayoutNodeID> {};
                    for (const auto& name : constructor.template_type_parameters) {
                        templates.push_back(concat({text("typename "), render_identifier(name)}));
                    }
                    rendered = concat(
                        {text("template"),
                         delimited_list(templates, "<", ">"),
                         builder.line(),
                         rendered}
                    );
                }
                return rendered;
            },
            [&](const TargetMemberFunctionDecl& function) noexcept {
                auto prefix = std::string {};
                if (function.friend_specifier) {
                    prefix += "friend ";
                }
                if (function.static_specifier) {
                    prefix += "static ";
                }
                if (function.constexpr_specifier) {
                    prefix += "constexpr ";
                }
                const auto function_name = render_member_function_name(function.name);
                const auto decltype_auto = text("decltype(auto)");
                auto result = SyntaxLayouts {
                    .inline_qualified = decltype_auto,
                    .wrapping = decltype_auto,
                };
                if (!function.decltype_auto_result) {
                    const auto reference = text(function.result_reference ? "&" : "");
                    result = render_type_layouts(function.result);
                    result.inline_qualified = concat({result.inline_qualified, reference});
                    result.wrapping = concat({result.wrapping, reference});
                }
                const auto signature = render_function_declarator(
                    prefix,
                    {.inline_qualified = function_name, .wrapping = function_name},
                    function.parameters,
                    result,
                    function.const_qualified
                );
                if (function.defaulted) {
                    return concat({signature, text(" = default;")});
                }
                if (function.declaration_only) {
                    return concat({signature, text(";")});
                }
                return concat({signature, text(" "), render_statement_block(function.body)});
            },
        },
        value
    );
}

auto TargetRenderer::render_declaration(const TargetDecl& value) noexcept -> LayoutNodeID {
    return std::visit(
        Overloaded {
            [&](const TargetFunctionDecl& function) noexcept {
                auto prefix = std::string {};
                if (function.inline_specifier) {
                    prefix += "inline ";
                }
                if (function.constexpr_specifier) {
                    prefix += "constexpr ";
                }
                const auto signature = render_function_declarator(
                    prefix,
                    render_name_layouts(function.name),
                    function.parameters,
                    render_type_layouts(function.result)
                );
                if (function.declaration_only) {
                    return concat({signature, text(";")});
                }
                return concat({signature, text(" "), render_statement_block(function.body)});
            },
            [&](const TargetStructDecl& structure) noexcept {
                auto members = std::vector<LayoutNodeID> {};
                for (const auto& member : structure.members) {
                    members.push_back(render_record_member(member));
                }
                return concat(
                    {text("struct "),
                     render_identifier(structure.name),
                     text(" "),
                     braced_block(members),
                     text(";")}
                );
            },
            [&](const TargetStructForwardDecl& structure) noexcept {
                return concat({text("struct "), render_identifier(structure.name), text(";")});
            },
            [&](const TargetEnumDecl& enumeration) noexcept {
                auto cases = std::vector<LayoutNodeID> {};
                cases.reserve(enumeration.cases.size());
                for (const auto& enum_case : enumeration.cases) {
                    cases.push_back(concat(
                        {render_identifier(enum_case.name),
                         text(" = "),
                         render_expression(enum_case.value),
                         text(",")}
                    ));
                }
                return concat(
                    {text("enum class "),
                     render_identifier(enumeration.name),
                     text(" : "),
                     render_type(enumeration.underlying_type),
                     text(" "),
                     braced_block(cases),
                     text(";")}
                );
            },
            [&](const TargetEnumForwardDecl& enumeration) noexcept {
                return concat(
                    {text("enum class "),
                     render_identifier(enumeration.name),
                     text(" : "),
                     render_type(enumeration.underlying_type),
                     text(";")}
                );
            },
            [&](const TargetClassDecl& target_class) noexcept {
                auto sections = std::vector<LayoutNodeID> {};
                for (const auto& section : target_class.sections) {
                    auto members = std::vector<LayoutNodeID> {};
                    for (const auto& member : section.members) {
                        members.push_back(render_class_member(member));
                    }
                    sections.push_back(concat(
                        {text(section.access == TargetClassAccess::Public ? "public:" : "private:"),
                         builder.indent(indent_width, concat({builder.line(), stack(members)}))}
                    ));
                }
                return concat(
                    {text("class "),
                     render_identifier(target_class.name),
                     text(target_class.final_specifier ? " final {" : " {"),
                     builder.line(),
                     stack(sections, 1),
                     builder.line(),
                     text("};")}
                );
            },
            [&](const TargetClassForwardDecl& target_class) noexcept {
                return concat({text("class "), render_identifier(target_class.name), text(";")});
            },
            [&](const TargetVariableDecl& variable) noexcept {
                const auto declarator = concat(
                    {render_name(variable.name),
                     delimited_list(std::array {render_expression(variable.initializer)}, "{", "}")}
                );
                return concat(
                    {text(variable.inline_specifier ? "inline " : ""),
                     text(variable.constexpr_specifier ? "constexpr " : ""),
                     render_type(variable.type),
                     text(" "),
                     declarator,
                     text(";")}
                );
            },
        },
        value
    );
}

auto TargetRenderer::render_item(TargetItemID id) noexcept -> LayoutNodeID {
    const auto& item = unit.item(id);
    const auto rendered = std::visit(
        Overloaded {
            [&](const TargetDecl& value) noexcept { return render_declaration(value); },
            [&](const TargetNamespace& value) noexcept {
                const auto nested = render_items(value.items);
                if (value.name.has_value()) {
                    const auto body_separator = [&]() noexcept {
                        return value.body_separation == TargetVerticalSeparation::BlankLine
                            ? concat({builder.line(), builder.line()})
                            : builder.line();
                    };
                    return concat(
                        {text("namespace "),
                         render_name(*value.name),
                         text(" {"),
                         body_separator(),
                         stack(nested, 1),
                         body_separator(),
                         generated_transition(),
                         text("} // namespace "),
                         render_name(*value.name)}
                    );
                }
                return concat({text("namespace "), braced_block(nested), text(" // namespace")});
            },
            [&](const TargetRawFragment& value) noexcept { return render_raw_fragment(value); },
            [&](const TargetItemGroup& value) noexcept {
                auto children = std::vector<LayoutNodeID> {};
                for (const auto child : value.items) {
                    children.push_back(render_item(child));
                }
                return stack(
                    children,
                    value.separation == TargetVerticalSeparation::BlankLine ? 1uz : 0uz
                );
            },
        },
        item.value
    );
    return with_attribution(rendered, item.attribution);
}
