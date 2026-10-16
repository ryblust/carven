import("benchmarks.harness.native_build", {rootdir = os.projectdir()})

function project_text(inputs, session)
    local options = session.options
    local lines = {native_build.project_header("carven-json-benchmark"), native_build.carven_rules(session, inputs),
        'set_policy("build.optimization.lto", false)',
        'target("validation")', '    set_kind("binary")', '    set_optimize("fastest")', '    add_defines("NDEBUG")'}
    local function call(name, values)
        local quoted = {}
        for _, value in ipairs(values) do table.insert(quoted, string.format("%q", value)) end
        table.insert(lines, "    " .. name .. "(" .. table.concat(quoted, ", ") .. ")")
    end
    if options.cxx then call("set_toolset", {"cxx", options.cxx}); call("set_toolset", {"ld", options.cxx}) end
    if options.portable then call("add_defines", {"CARVEN_SIMD_FORCE_PORTABLE=1"}) end
    table.insert(lines, '    add_rules("@carven/carven", {linkage_domain = "benchmark:json"})')
    call("add_files", {path.join(inputs, "benchmarks/json/main.cpp"), "workload.cv"})
    table.insert(lines, "target_end()")
    return table.concat(lines, "\n") .. "\n"
end
