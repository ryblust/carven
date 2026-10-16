import("benchmarks.harness.native_build", {rootdir = os.projectdir()})

function project_text(inputs, case, session)
    local lines = {native_build.project_header("carven-simd-benchmark"),
        native_build.carven_rules(session, inputs), "set_policy(\"build.optimization.lto\", false)"}
    local cxx, portable = session.options.cxx, session.options.portable
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
        call("add_includedirs", {path.join(inputs, "crafts"), path.join(inputs, "benchmarks", "simd")})
        call("add_files", {path.join(inputs, "benchmarks", "simd", "main.cpp")})
        if implementation == "carven" then
            table.insert(lines, string.format('    add_rules("@carven/carven", {linkage_domain = %q})', "benchmark:simd:" .. case.id))
            call("add_files", {"workload.cv"})
        else
            call("add_files", {path.join(inputs, "benchmarks", "simd", "byte_loop.cpp")})
        end
        table.insert(lines, "target_end()")
    end
    return table.concat(lines, "\n") .. "\n"
end
