export module zero.driver.handler;

import zero.common.file;
import zero.common.source;
import zero.driver.pipeline;
import zero.frontend.lexer;
import zero.frontend.parser;
import std;

export auto run(const Driver& driver) -> int {
    if (const auto content = read_file(driver.input_files[0]); content) {
        return driver.run_single_file({
            .filename = std::string(driver.input_files[0]), .content = std::move(*content)
        });
    } else {
        std::println("zero run: error: cannot read '{}'", driver.input_files[0]);
        return 1;
    }
}

export auto tokens(const Driver& driver) -> int {
    if (const auto content = read_file(driver.input_files[0]); content) {
        std::println("{}", tokenize(*content));
        return 0;
    } else {
        std::println("zero tokens: error: cannot read '{}'", driver.input_files[0]);
        return 1;
    }
}

export auto ast([[maybe_unused]] const Driver& driver) -> int {
    std::println("zero ast: not yet implemented");
    return 0;
}

export auto build([[maybe_unused]] const Driver& driver) -> int {
    std::println("zero build: not yet implemented");
    return 0;
}

export auto check([[maybe_unused]] const Driver& driver) -> int {
    std::println("zero check: not yet implemented");
    return 0;
}
