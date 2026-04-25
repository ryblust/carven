export module zero.driver.pipeline;

import zero.frontend.lexer;
import zero.frontend.parser;
import zero.backend.codegen;
import std;

export constexpr auto transpile(std::string_view source) noexcept -> std::string {
    const auto tokens = tokenize(source);
    const auto items  = parse(tokens, source);
    return generate(items, source);
}
