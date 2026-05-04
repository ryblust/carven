export module zero.driver.handler;

import zero.common.file;
import zero.common.source;
import zero.driver.pipeline;
import zero.frontend.lexer;
import zero.frontend.parser;
import std;

export auto run(std::span<const char* const> args, const Driver& driver) -> int {
    if (args.empty()) {
        std::println("zero run: error: no input file");
        return 1;
    }

    if (const auto content = read_file(args[0]); content) {
        return driver.run_single_file({ .filename = args[0], .content = std::move(*content) });
    } else {
        std::println("zero run: error: cannot read '{}'", args[0]);
        return 1;
    }
}

export auto tokens(std::span<const char* const> args, [[maybe_unused]] const Driver& driver) -> int {
    if (args.empty()) {
        std::println("zero tokens: error: no input file");
        return 1;
    }

    if (const auto content = read_file(args[0]); content) {
        for (const auto token : tokenize(*content)) {
            std::println("{}", token);
        }
        return 0;
    } else {
        std::println("zero tokens: error: cannot read '{}'", args[0]);
        return 1;
    }
}

export auto ast([[maybe_unused]] std::span<const char* const> args, [[maybe_unused]] const Driver& driver) -> int {
    std::println("zero ast: not yet implemented");
    return 0;
}

export auto build([[maybe_unused]] std::span<const char* const> args, [[maybe_unused]] const Driver& driver) -> int {
    std::println("zero build: not yet implemented");
    return 0;
}

export auto check([[maybe_unused]] std::span<const char* const> args, [[maybe_unused]] const Driver& driver) -> int {
    std::println("zero check: not yet implemented");
    return 0;
}
