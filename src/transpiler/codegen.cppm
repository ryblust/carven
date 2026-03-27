export module zero.transpiler.codegen;

import zero.transpiler.parser;
import std;

constexpr auto codegen_from(const ImportModuleInfo& info) noexcept -> std::string {
    auto result = std::string("import ").append(info.module_name).append(";\n");

    if (!info.using_decls.empty()) {
        for (auto i = 0uz; i < info.using_decls.size(); i++) {
            result
                .append("\nusing ")
                .append(info.module_name).append("::")
                .append(info.using_decls[i]).append(";\n");
        }
    }

    return result;
}

export constexpr auto codegen() noexcept -> std::string {
    return {};
}