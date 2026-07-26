module carven:frontend.dump.ast.decl.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.region;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.type;
import :frontend.dump.ast;
import :source.text;
import :support.visit;
import std;

auto ASTDumper::render_import(
    ASTImportID declaration_id,
    std::string_view prefix,
    bool is_last
) noexcept -> void {
    const auto& declaration = ast.import_declaration(declaration_id);
    append_line(
        prefix,
        is_last,
        std::format("ImportDeclaration {}", format_dump_span(declaration.span))
    );
    const auto nested_prefix = child_prefix(prefix, is_last);
    const auto& reference = declaration.module_reference;
    const auto reference_kind = std::visit(
        Overloaded {
            [](const ASTDomainRootModuleReference&) static noexcept {
                return std::string_view("DomainRoot");
            },
            [](const ASTParentRelativeModuleReference&) static noexcept {
                return std::string_view("ParentRelative");
            },
            [](const ASTCraftQualifiedModuleReference&) static noexcept {
                return std::string_view("CraftQualified");
            },
        },
        reference.value
    );
    append_line(
        nested_prefix,
        false,
        std::format("module_reference {} {}", reference_kind, format_dump_span(reference.span))
    );
    const auto reference_prefix = child_prefix(nested_prefix, false);
    std::visit(
        Overloaded {
            [&](const auto& value) noexcept {
                if constexpr (requires { value.name_span; }) {
                    render_span_field(reference_prefix, false, "craft", value.name_span);
                    render_span_field(reference_prefix, false, "separator", value.separator_span);
                }
                if constexpr (requires { value.prefix_span; }) {
                    render_span_field(reference_prefix, false, "prefix", value.prefix_span);
                }
                render_list(
                    reference_prefix,
                    true,
                    "components",
                    value.components,
                    [&](Span component, std::string_view item_prefix, bool item_last) noexcept {
                        render_span_field(item_prefix, item_last, "component", component);
                    }
                );
            },
        },
        reference.value
    );
    std::visit(
        Overloaded {
            [&](const ASTSingleImport& selection) noexcept {
                append_line(
                    nested_prefix,
                    true,
                    std::format(
                        "selection SingleImport {}",
                        format_dump_span(declaration.selection.span)
                    )
                );
                render_span_field(
                    child_prefix(nested_prefix, true),
                    true,
                    "name",
                    selection.name_span
                );
            },
            [&](ASTWildcardImport) noexcept {
                append_line(
                    nested_prefix,
                    true,
                    std::format(
                        "selection WildcardImport {}",
                        format_dump_span(declaration.selection.span)
                    )
                );
            },
            [&](const ASTImportList& selection) noexcept {
                append_line(
                    nested_prefix,
                    true,
                    std::format(
                        "selection ImportList {}",
                        format_dump_span(declaration.selection.span)
                    )
                );
                render_list(
                    child_prefix(nested_prefix, true),
                    true,
                    "names",
                    selection.names,
                    [&](Span name, std::string_view item_prefix, bool item_last) noexcept {
                        render_span_field(item_prefix, item_last, "name", name);
                    }
                );
            },
        },
        declaration.selection.value
    );
}

auto ASTDumper::render_top_level_item(
    ASTItemID item_id,
    std::string_view prefix,
    bool is_last
) noexcept -> void {
    const auto& item = ast.item(item_id);
    const auto render_visibility = [&](const ASTDeclarationVisibility& visibility,
                                       std::string_view nested_prefix) noexcept {
        std::visit(
            Overloaded {
                [&](const ASTPrivateDeclarationVisibility& value) noexcept {
                    render_span_field(
                        nested_prefix,
                        false,
                        "visibility Private",
                        value.keyword_span
                    );
                },
                [&](ASTBareDeclarationVisibility) noexcept {
                    append_line(nested_prefix, false, "visibility Bare");
                },
                [&](const ASTExportDeclarationVisibility& value) noexcept {
                    render_span_field(
                        nested_prefix,
                        false,
                        "visibility Export",
                        value.keyword_span
                    );
                },
            },
            visibility
        );
    };
    std::visit(
        Overloaded {
            [&](const ASTEnumDecl& declaration) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("EnumDeclaration {}", format_dump_span(item.span))
                );
                const auto nested_prefix = child_prefix(prefix, is_last);
                render_visibility(declaration.visibility, nested_prefix);
                render_span_field(nested_prefix, false, "name", declaration.name_span);
                if (!declaration.underlying_type.has_value()) {
                    append_line(nested_prefix, false, "underlying_type <absent>");
                } else {
                    render_type(
                        *declaration.underlying_type,
                        nested_prefix,
                        false,
                        "underlying_type "
                    );
                }
                render_list(
                    nested_prefix,
                    true,
                    "cases",
                    declaration.cases,
                    [&](const ASTEnumCase& member,
                        std::string_view item_prefix,
                        bool item_last) noexcept {
                        append_line(
                            item_prefix,
                            item_last,
                            std::format("EnumCase {}", format_dump_span(member.span))
                        );
                        const auto member_prefix = child_prefix(item_prefix, item_last);
                        render_span_field(member_prefix, false, "name", member.name_span);
                        render_list(
                            member_prefix,
                            !member.initializer.has_value(),
                            "payload_types",
                            member.payload_types,
                            [&](ASTTypeID type,
                                std::string_view type_prefix,
                                bool type_last) noexcept {
                                render_type(type, type_prefix, type_last);
                            }
                        );
                        if (member.initializer.has_value()) {
                            render_expression(
                                *member.initializer,
                                member_prefix,
                                true,
                                "initializer "
                            );
                        }
                    }
                );
            },
            [&](const ASTStructDecl& declaration) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("StructDeclaration {}", format_dump_span(item.span))
                );
                const auto nested_prefix = child_prefix(prefix, is_last);
                render_visibility(declaration.visibility, nested_prefix);
                render_span_field(nested_prefix, false, "name", declaration.name_span);
                render_list(
                    nested_prefix,
                    true,
                    "fields",
                    declaration.fields,
                    [&](const ASTStructField& field,
                        std::string_view item_prefix,
                        bool item_last) noexcept {
                        append_line(
                            item_prefix,
                            item_last,
                            std::format("StructField {}", format_dump_span(field.span))
                        );
                        const auto field_prefix = child_prefix(item_prefix, item_last);
                        render_span_field(field_prefix, false, "name", field.name_span);
                        render_type(field.type, field_prefix, true, "type ");
                    }
                );
            },
            [&](const ASTFunctionDecl& definition) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("FunctionDefinition {}", format_dump_span(item.span))
                );
                const auto nested_prefix = child_prefix(prefix, is_last);
                render_visibility(definition.visibility, nested_prefix);
                render_span_field(nested_prefix, false, "name", definition.name_span);
                render_list(
                    nested_prefix,
                    false,
                    "parameters",
                    definition.parameters,
                    [&](const ASTFunctionParameter& parameter,
                        std::string_view item_prefix,
                        bool item_last) noexcept {
                        append_line(
                            item_prefix,
                            item_last,
                            std::format("FunctionParameter {}", format_dump_span(parameter.span))
                        );
                        const auto parameter_prefix = child_prefix(item_prefix, item_last);
                        if (parameter.access.marker.has_value()) {
                            render_span_field(
                                parameter_prefix,
                                false,
                                "access marker",
                                *parameter.access.marker
                            );
                        } else {
                            append_line(parameter_prefix, false, "access Read");
                        }
                        render_span_field(
                            parameter_prefix,
                            !parameter.type.has_value(),
                            std::holds_alternative<ASTNamedBindingTarget>(parameter.target)
                                ? "name"
                                : "discard",
                            binding_target_span(parameter.target)
                        );
                        if (parameter.type.has_value()) {
                            render_type(*parameter.type, parameter_prefix, true, "type ");
                        }
                    }
                );
                if (!definition.result_type.has_value()) {
                    append_line(nested_prefix, false, "result <absent>");
                } else {
                    render_type(*definition.result_type, nested_prefix, false, "result ");
                }
                render_throw_clause(definition.throw_clause, nested_prefix, false);
                render_ordinary_block(definition.body, nested_prefix, true, "body ");
            },
            [&](const ASTConstantDecl& declaration) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("ConstantDeclaration {}", format_dump_span(item.span))
                );
                const auto nested_prefix = child_prefix(prefix, is_last);
                render_visibility(declaration.visibility, nested_prefix);
                render_span_field(nested_prefix, false, "name", declaration.name_span);
                if (declaration.type.has_value()) {
                    render_type(*declaration.type, nested_prefix, false, "type ");
                } else {
                    append_line(nested_prefix, false, "type <absent>");
                }
                render_expression(declaration.initializer, nested_prefix, true, "initializer ");
            },
            [&](const ASTTestDecl& declaration) noexcept {
                append_line(
                    prefix,
                    is_last,
                    std::format("TestDeclaration {}", format_dump_span(item.span))
                );
                const auto nested_prefix = child_prefix(prefix, is_last);
                render_span_field(nested_prefix, false, "keyword", declaration.keyword_span);
                render_span_field(nested_prefix, false, "name", declaration.name_span);
                render_ordinary_block(declaration.body, nested_prefix, true, "body ");
            },
            [&](const CppRegion& region) noexcept { render_cpp_region(region, prefix, is_last); },
        },
        item.value
    );
}
