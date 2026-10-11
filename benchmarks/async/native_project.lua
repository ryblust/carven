import("benchmarks.harness.native_build", {rootdir = os.projectdir()})

function project_text(defines, implementations, root, include_root, cxx)
    local lines = {native_build.project_header("carven-async-benchmark"), "add_cxxflags(\"-fno-rtti\")"}
    local function call(name, arguments)
        local values = {}
        for _, value in ipairs(arguments) do table.insert(values, string.format("%q", value)) end
        table.insert(lines, "    " .. name .. "(" .. table.concat(values, ", ") .. ")")
    end
    local profiles = {"timing", "allocations", "stack", "noexceptions"}
    local function configure_profile(profile)
        table.insert(lines, "    set_default(false)")
        if cxx then
            call("set_toolset", {"cxx", cxx})
            call("set_toolset", {"ld", cxx})
        end
        table.insert(lines, "    set_optimize(" .. (profile == "stack" and "\"none\"" or "\"fastest\"") .. ")")
        if profile == "stack" then call("add_cxxflags", {"-fno-optimize-sibling-calls"})
        else call("add_defines", {"NDEBUG"}) end
        if profile == "noexceptions" then table.insert(lines, "    set_exceptions(\"no-cxx\")") end
        call("add_defines", {"CARVEN_ASYNC_BENCH_ALLOCATIONS=" .. (profile == "allocations" and "1" or "0")})
        call("add_defines", {"CARVEN_ASYNC_BENCH_STACK=" .. (profile == "stack" and "1" or "0")})
        call("add_defines", defines)
        call("add_includedirs", {path.join(include_root, "benchmarks", "async"),
            path.join(include_root, "crafts"), path.join(root, "generated")})
    end
    for _, profile in ipairs(profiles) do
        table.insert(lines, string.format("target(%q)", "measurement-" .. profile))
        table.insert(lines, "    set_kind(\"object\")")
        configure_profile(profile)
        call("add_files", {path.join(include_root, "benchmarks", "async", "measurement.cpp")})
        table.insert(lines, "target_end()")
    end
    for _, implementation in ipairs(implementations) do
        for _, profile in ipairs(profiles) do
            if profile ~= "noexceptions" or implementation == "carven" then
                table.insert(lines, string.format("target(%q)", implementation .. "-" .. profile))
                table.insert(lines, "    set_kind(\"binary\")")
                configure_profile(profile)
                call("add_deps", {"measurement-" .. profile})
                if implementation == "carven" then
                    call("add_files", {path.join(root, "generated", "**.cpp")})
                else
                    local source = implementation == "carven_runtime" and "runtime_workload.cpp" or "workload.cpp"
                    call("add_files", {path.join(include_root, "benchmarks", "async", source)})
                    call("add_defines", {"CARVEN_ASYNC_BENCH_LIBCORO=" .. (implementation == "libcoro" and "1" or "0")})
                    if implementation == "libcoro" then
                        call("add_includedirs", {path.join(include_root, "libcoro", "include")})
                    end
                end
                table.insert(lines, "target_end()")
            end
        end
    end
    return table.concat(lines, "\n") .. "\n"
end
