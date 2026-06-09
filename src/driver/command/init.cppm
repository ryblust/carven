export module carven.driver.command.init;

import carven.driver.pipeline;
import carven.driver.request;
import std;

export auto execute(const InitRequest& request, std::string_view carven_executable) noexcept -> int {
    return init_project(request.project_dir, request.language_standard, carven_executable);
}
