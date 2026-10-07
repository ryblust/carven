import("benchmarks.harness.native_build", {rootdir = os.projectdir()})

function project_text(root, inputs, case, cxx, portable)
    local lines = {native_build.project_header("carven-simd-benchmark"), "set_policy(\"build.optimization.lto\", false)"}
    local function call(name, values)
        local quoted = {}
        for _, value in ipairs(values) do table.insert(quoted, string.format("%q", value)) end
        table.insert(lines, "    " .. name .. "(" .. table.concat(quoted, ", ") .. ")")
    end
    for _, implementation in ipairs({"carven", "byte_loop"}) do
        table.insert(lines, string.format("target(%q)", implementation))
        table.insert(lines, "    set_kind(\"binary\")\n    set_optimize(\"fastest\")")
        if cxx then call("set_toolset", {"cxx", cxx}); call("set_toolset", {"ld", cxx}) end
        call("add_defines", {"NDEBUG", "CARVEN_SIMD_BENCH_CARVEN=" .. (implementation == "carven" and "1" or "0"),
            "CARVEN_SIMD_BENCH_DIGITS=" .. (case.set == "digits" and "1" or "0"),
            "CARVEN_SIMD_BENCH_FIELD=" .. (case.set == "field" and "1" or "0"),
            "CARVEN_SIMD_BENCH_COUNT=" .. (case.operation == "count" and "1" or "0"),
            "CARVEN_SIMD_BENCH_FIND=" .. (case.operation == "find" and "1" or "0")})
        if portable then call("add_defines", {"CARVEN_SIMD_FORCE_PORTABLE=1"}) end
        call("add_includedirs", {path.join(inputs, "crafts"), path.join(inputs, "benchmarks", "simd"), path.join(root, "generated")})
        call("add_files", {path.join(inputs, "benchmarks", "simd", "main.cpp")})
        call("add_files", {implementation == "carven" and path.join(root, "generated", "**.cpp")
            or path.join(inputs, "benchmarks", "simd", "byte_loop.cpp")})
        table.insert(lines, "target_end()")
    end
    return table.concat(lines, "\n") .. "\n"
end
