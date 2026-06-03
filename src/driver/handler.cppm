export module carven.driver.handler;

import carven.common.source;
import carven.driver.pipeline;
import std;

export auto run(const Driver& driver) noexcept -> int {
    if (auto source = SourceFile::from_file(driver.input_files[0]); source) {
        return run_single_file(driver, std::move(*source));
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
