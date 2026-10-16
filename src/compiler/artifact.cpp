module carven:compiler.artifact.impl;

import :compiler.artifact;
import :semantic.analysis.program.checked;
import :semantic.artifact;
import :semantic.semir.program;
import :source.provenance;
import :support.binary;
import std;

namespace {

struct ArtifactContext final {};

constexpr auto compilation_artifact_kind = 3u;

} // namespace

auto encode_compilation_artifact(
    const CheckedUnit& unit,
    std::span<const CompilationModuleOwnership> modules
) noexcept -> std::expected<std::vector<std::byte>, BinaryError> {
    auto writer = BinaryWriter();
    if (modules.size() != unit.program.provenance().module_records().size()) {
        writer.fail("compilation ownership does not match semantic modules");
        return std::unexpected(*writer.error());
    }
    auto program = encode_semir_program(unit.program);
    if (!program) {
        return std::unexpected(std::move(program.error()));
    }
    auto context = ArtifactContext {};
    writer.unsigned_value(compilation_artifact_kind, 4);
    writer.unsigned_value(modules.size(), 8);
    for (const auto& module : modules) {
        writer.write(module, context);
    }
    writer.write(unit.cacheable, context);
    writer.unsigned_value(program->size(), 8);
    writer.append(*program);
    writer.unsigned_value(unit.definitions.size(), 8);
    writer.append(unit.definitions);
    if (writer.error()) {
        return std::unexpected(*writer.error());
    }
    return std::move(writer).take();
}

auto read_compilation_artifact(std::span<const std::byte> bytes) noexcept
    -> std::expected<CompilationArtifact, BinaryError> {
    auto reader = BinaryReader(bytes);
    auto context = ArtifactContext {};
    auto kind = reader.unsigned_value(4);
    if (!kind) {
        return std::unexpected(std::move(kind.error()));
    }
    if (*kind != compilation_artifact_kind) {
        return reader.fail("invalid compilation artifact kind");
    }
    auto modules = reader.read<std::vector<CompilationModuleOwnership>>(context);
    if (!modules) {
        return std::unexpected(std::move(modules.error()));
    }
    auto cacheable = reader.read<bool>(context);
    if (!cacheable) {
        return std::unexpected(std::move(cacheable.error()));
    }
    auto program_size = reader.unsigned_value(8);
    if (!program_size) {
        return std::unexpected(std::move(program_size.error()));
    }
    if (*program_size > reader.remaining()) {
        return reader.fail("invalid compilation program size");
    }
    auto program = reader.take(static_cast<std::size_t>(*program_size));
    if (!program) {
        return std::unexpected(std::move(program.error()));
    }
    auto definitions_size = reader.unsigned_value(8);
    if (!definitions_size) {
        return std::unexpected(std::move(definitions_size.error()));
    }
    if (*definitions_size != reader.remaining()) {
        return reader.fail("invalid compilation definitions size");
    }
    auto definitions = reader.take(static_cast<std::size_t>(*definitions_size));
    if (!definitions) {
        return std::unexpected(std::move(definitions.error()));
    }
    return CompilationArtifact {
        .modules = std::move(*modules),
        .cacheable = *cacheable,
        .program = *program,
        .definitions = *definitions,
    };
}
