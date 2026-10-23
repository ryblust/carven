target("carven-analyzer")
    set_group("toolchain")
    set_default(false)
    set_kind("binary")
    if is_mode("release") then
        set_policy("build.optimization.lto", true)
    end
    add_deps("carven-workspace-modules")
    add_files("src/*.cpp", "src/*.cppm")
target_end()
