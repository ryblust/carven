export module carven.driver.handler;

import carven.common.source;
import carven.driver.pipeline;
import carven.driver.toolchain;
import std;

auto source_file_exists(std::string_view path) noexcept -> bool {
    auto error = std::error_code();
    return std::filesystem::exists(std::filesystem::path(path), error) && !error;
}

export auto run(const Driver& driver) noexcept -> int {
    if (driver.input_files.empty() || !is_carven_source_path(driver.input_files[0])) {
        return run_project(driver);
    }

    if (!source_file_exists(driver.input_files[0])) {
        std::println("carven run: error: cannot read '{}'", driver.input_files[0]);
        return 1;
    }

    return run_single_file(driver, driver.input_files[0]);
}

export auto build(const Driver& driver) noexcept -> int {
    if (driver.input_files.empty() || !is_carven_source_path(driver.input_files[0])) {
        return build_project(driver);
    }

    if (!source_file_exists(driver.input_files[0])) {
        std::println("carven build: error: cannot read '{}'", driver.input_files[0]);
        return 1;
    }

    return build_single_file(driver, driver.input_files[0]);
}

export auto init(const Driver& driver) noexcept -> int {
    return init_project(driver);
}

export auto transpile_command(const Driver& driver) noexcept -> int {
    if (driver.input_files.empty()) {
        std::println("carven transpile: error: no input file");
        return 1;
    }

    if (driver.output_file && driver.input_files.size() != 1) {
        std::println("carven transpile: error: '-o' requires exactly one input file");
        return 1;
    }

    if (driver.output_file) {
        const auto input = driver.input_files[0];
        if (auto source = SourceFile::from_file(input); source) {
            return write_transpiled_source(driver, source->text(), source->filepath(), *driver.output_file);
        } else {
            std::println("carven transpile: error: cannot read '{}'", input);
            return 1;
        }
    }

    auto result = 0;
    for (auto i = 0uz; i < driver.input_files.size(); ++i) {
        const auto input = driver.input_files[i];
        if (auto source = SourceFile::from_file(input); source) {
            if (i > 0) std::print("\n");
            result = std::max(result, print_transpiled_source(driver, source->text(), source->filepath()));
        } else {
            std::println("carven transpile: error: cannot read '{}'", input);
            result = 1;
        }
    }

    return result;
}

export auto check([[maybe_unused]] const Driver& driver) noexcept -> int {
    std::println("carven check: not yet implemented");
    return 1;
}
