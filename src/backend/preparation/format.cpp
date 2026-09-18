module carven:backend.preparation.format.impl;

import :backend.preparation.format;
import :semantic.format.builtin;
import :semantic.format;
import std;

auto writer_field_operand_count(const WriterFormatField& field) noexcept -> std::size_t {
    const auto* integer = std::get_if<IntegerFormatField>(&field);
    return integer != nullptr && !integer->static_width ? 2uz : 1uz;
}

auto prepared_format_operands(const PreparedFormat& preparation) noexcept
    -> std::span<const std::size_t> {
    return preparation.visit([](const auto& value) static noexcept -> std::span<const std::size_t> {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, PreparedFormatText>) {
            return {};
        } else {
            return value.operand_indices;
        }
    });
}

auto prepare_format(
    const ConstantValueReader& values,
    const FormatSpec& specification,
    std::span<const std::optional<BuiltinType>> types,
    std::span<const std::optional<ConstantID>> known
) noexcept -> PreparedFormat {
    const auto can_reduce = std::ranges::all_of(types, [](const auto type) static noexcept {
        return type
            && (builtin_is_numeric(*type)
                || *type == BuiltinType::Bool
                || *type == BuiltinType::Char
                || *type == BuiltinType::Str
                || *type == BuiltinType::String);
    });
    if (!can_reduce) {
        return PreparedDelegatedFormat {
            .format_string = serialize_format(specification),
            .operand_indices = format_operands(specification),
            .encoding = FormatResultEncoding::Unproven
        };
    }
    auto arguments = std::vector<BuiltinFormatValue>();
    for (const auto value : known) {
        arguments.push_back(
            value ? builtin_format_value(values, values.constant(*value)) : BuiltinFormatValue {}
        );
    }
    auto prepared = FormatSpec {};
    auto text_size = 0uz;
    auto within_budget = true;
    const auto append_text = [&](std::string bytes) noexcept {
        if (bytes.size()
            > maximum_prepared_format_bytes - std::min(text_size, maximum_prepared_format_bytes)) {
            within_budget = false;
            return;
        } else {
            text_size += bytes.size();
        }
        if (!prepared.parts.empty()) {
            if (auto* previous = std::get_if<FormatText>(&prepared.parts.back().value)) {
                previous->bytes += bytes;
                return;
            }
        }
        prepared.parts.push_back({FormatText {.bytes = std::move(bytes)}});
    };
    for (const auto& part : specification.parts) {
        if (!within_budget) {
            break;
        }
        if (const auto* text = std::get_if<FormatText>(&part.value)) {
            append_text(text->bytes);
            continue;
        }
        auto hole = std::get<FormatHole>(part.value);
        const auto evaluated = resolve_builtin_format_specification(
            hole,
            arguments,
            maximum_prepared_format_bytes,
            types[hole.operand_index]
                && (*types[hole.operand_index] == BuiltinType::F32
                    || *types[hole.operand_index] == BuiltinType::F64)
        );
        if (evaluated) {
            if (known[hole.operand_index]) {
                auto folded = format_builtin_value(
                    arguments[hole.operand_index],
                    *evaluated,
                    maximum_prepared_format_bytes
                );
                if (folded) {
                    append_text(std::move(*folded));
                    continue;
                }
            }
            if (types[hole.operand_index] && builtin_is_numeric(*types[hole.operand_index])) {
                hole.specification = {{FormatText {.bytes = *evaluated}}};
                hole.has_specification = !evaluated->empty();
            }
        }
        prepared.parts.push_back({std::move(hole)});
    }
    if (!within_budget) {
        prepared = specification;
    }
    auto retained = format_operands(prepared);
    if (retained.empty() && within_budget) {
        auto text = std::string();
        for (const auto& part : prepared.parts) {
            text += std::get<FormatText>(part.value).bytes;
        }
        return PreparedFormatText {.text = std::move(text)};
    }
    auto retained_types = std::vector<std::optional<BuiltinType>>();
    for (const auto index : retained) {
        retained_types.push_back(types[index]);
    }
    auto next = 0uz;
    const auto renumber = [&](this const auto& self, std::span<FormatPart> parts) noexcept -> void {
        for (auto& part : parts) {
            if (auto* hole = std::get_if<FormatHole>(&part.value)) {
                hole->operand_index = next++;
                self(hole->specification);
            }
        }
    };
    renumber(prepared.parts);
    // Direct writing consumes structured text, not an escaped native format string.
    if (auto writer = classify_writer_format(prepared, retained_types)) {
        return PreparedWriterFormat {
            .format = std::move(*writer),
            .operand_indices = std::move(retained)
        };
    }
    auto serialized = serialize_format(prepared, maximum_prepared_format_bytes);
    if (!serialized) {
        return PreparedDelegatedFormat {
            .format_string = serialize_format(specification),
            .operand_indices = format_operands(specification),
            .encoding = format_preserves_utf8(specification, types)
                ? FormatResultEncoding::ValidUTF8
                : FormatResultEncoding::Unproven,
        };
    }
    const auto encoding = format_preserves_utf8(prepared, retained_types)
        ? FormatResultEncoding::ValidUTF8
        : FormatResultEncoding::Unproven;
    return PreparedDelegatedFormat {
        .format_string = std::move(*serialized),
        .operand_indices = std::move(retained),
        .encoding = encoding,
    };
}
