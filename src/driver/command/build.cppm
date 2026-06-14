export module carven.driver.command.build;

import carven.driver.pipeline;
import carven.driver.request;
import std;

inline auto source_file_exists(std::string_view path) noexcept -> bool {
    auto error = std::error_code();
    return std::filesystem::is_regular_file(std::filesystem::path(path), error) && !error;
}

export auto execute(const BuildRequest& request, std::string_view carven_executable) noexcept -> int {
    const auto options = TranspileOptions {
        .language_standard = request.language_standard,
        .import_std = request.import_std,
    };

    return std::visit([&](const auto& mode) noexcept -> int {
        using Mode = std::remove_cvref_t<decltype(mode)>;
        if constexpr (std::same_as<Mode, SingleFileBuild>) {
            if (!source_file_exists(mode.source_file)) {
                std::println("carven build: error: cannot read '{}'", mode.source_file);
                return 1;
            }
            return build_single_file({
                .source_file = mode.source_file,
                .carven_executable = carven_executable,
                .options = options,
            });
        } else {
            return build_project(mode.target);
        }
    }, request.mode);
}
