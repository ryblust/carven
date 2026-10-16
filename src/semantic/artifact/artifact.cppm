module carven:semantic.artifact;

import :semantic.analysis.catalog;
import :semantic.analysis.program;
import :semantic.semir.program;
import :support.binary;
import std;

auto capture_checked_definitions(const ProgramDraft& draft, const AnalysisCatalog& catalog) noexcept
    -> std::expected<std::vector<std::byte>, BinaryError>;

auto decode_checked_definitions(std::span<const std::byte> bytes) noexcept
    -> std::expected<CheckedDefinitions, BinaryError>;

auto encode_semir_program(const SemIRProgram& program) noexcept
    -> std::expected<std::vector<std::byte>, BinaryError>;

auto decode_semir_program(std::span<const std::byte> bytes) noexcept
    -> std::expected<SemIRProgram, BinaryError>;
