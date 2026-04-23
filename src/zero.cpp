import zero.frontend.lexer;
import zero.frontend.parser;
import zero.backend.emit;
import zero.driver.cli;

import std;

static constexpr const char SOURCE[] = {
    #embed "tests/helloworld.zero"
};

auto main(int argc, char** argv) noexcept -> int {
    const auto tokens = tokenize(SOURCE);
    const auto blocks = blockify(tokens);
    const auto items  = parse_blocks(blocks);
    const auto output = codegen(items);

    std::print("{}", output);
}