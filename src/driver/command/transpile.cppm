export module carven.driver.command.transpile;

import carven.common.source;
import carven.driver.pipeline;
import carven.driver.request;
import std;

export auto execute(const TranspileRequest& request) noexcept -> int {
    const auto options = TranspileOptions {
        .language_standard = request.language_standard,
        .import_std = request.import_std,
    };

    if (request.output_file) {
        const auto input = request.source_files[0];
        if (auto source = SourceFile::from_file(input); source) {
            return write_transpiled_source({
                .source = source->text(),
                .filepath = source->filepath(),
                .output_path = *request.output_file,
                .options = options,
            });
        } else {
            std::println("carven transpile: error: cannot read '{}'", input);
            return 1;
        }
    }

    auto result = 0;
    for (auto i = 0uz; i < request.source_files.size(); ++i) {
        const auto input = request.source_files[i];
        if (auto source = SourceFile::from_file(input); source) {
            if (i > 0) std::print("\n");
            result = std::max(result, print_transpiled_source(source->text(), source->filepath(), options));
        } else {
            std::println("carven transpile: error: cannot read '{}'", input);
            result = 1;
        }
    }

    return result;
}
