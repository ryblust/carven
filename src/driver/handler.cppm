export module carven.driver.handler;

import carven.common.filesystem;
import carven.common.source;
import carven.driver.pipeline;
import std;

export auto run(const Driver& driver) noexcept -> int {
    if (const auto content = read_file(driver.input_files[0]); content) {
        return run_single_file(driver, {
            .filename = std::string(driver.input_files[0]), .content = std::move(*content)
        });
    } else {
        std::println("carven run: error: cannot read '{}'", driver.input_files[0]);
        return 1;
    }
}

export auto build([[maybe_unused]] const Driver& driver) noexcept -> int {
    std::println("carven build: not yet implemented");
    return 0;
}

export auto check([[maybe_unused]] const Driver& driver) noexcept -> int {
    std::println("carven check: not yet implemented");
    return 0;
}
