module carven:support.dump;

import std;

auto escape_dump_value(std::string_view value) noexcept -> std::string;
auto quote_dump_value(std::string_view value) noexcept -> std::string;
