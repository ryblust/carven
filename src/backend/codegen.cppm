export module zero.backend.codegen;

import zero.frontend.parser.ast;
import std;

export auto generate(std::span<const TopLevelItem> items, std::string_view source, std::uint8_t standard, bool default_include_std) noexcept -> std::string;
