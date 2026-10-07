import("benchmarks.harness.native_build", {rootdir = os.projectdir()})

function project_text(root, inputs, options)
    local lines = {native_build.project_header("carven-json-benchmark"), 'set_policy("build.optimization.lto", false)',
        'target("validation")', '    set_kind("binary")', '    set_optimize("fastest")', '    add_defines("NDEBUG")'}
    local function call(name, values)
        local quoted = {}
        for _, value in ipairs(values) do table.insert(quoted, string.format("%q", value)) end
        table.insert(lines, "    " .. name .. "(" .. table.concat(quoted, ", ") .. ")")
    end
    if options.cxx then call("set_toolset", {"cxx", options.cxx}); call("set_toolset", {"ld", options.cxx}) end
    if options.portable then call("add_defines", {"CARVEN_SIMD_FORCE_PORTABLE=1"}) end
    call("add_includedirs", {path.join(inputs, "crafts"), path.join(root, "generated")})
    call("add_files", {path.join(inputs, "benchmarks/json/main.cpp"), path.join(root, "generated/**.cpp")})
    table.insert(lines, "target_end()")
    return table.concat(lines, "\n") .. "\n"
end
