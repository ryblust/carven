includes("workloads.lua")
local workload_cases = cases()

for _, variant in ipairs({"native", "portable"}) do
    namespace("benchmarks::simd::" .. variant, function ()
        set_languages("c++20")
        set_exceptions("cxx")
        if variant == "portable" then
            add_defines("CARVEN_SIMD_FORCE_PORTABLE=1")
        end

        set_policy("build.optimization.lto", false)
        set_optimize("fastest")
        add_defines("NDEBUG")

        for _, case in ipairs(workload_cases) do
            local workload_defines = {
                "CARVEN_SIMD_BENCH_DIGITS=" .. (case.set == "digits" and "1" or "0"),
                "CARVEN_SIMD_BENCH_FIELD=" .. (case.set == "field" and "1" or "0"),
                "CARVEN_SIMD_BENCH_COUNT=" .. (case.operation == "count" and "1" or "0"),
                "CARVEN_SIMD_BENCH_FIND=" .. (case.operation == "find" and "1" or "0"),
            }
            for _, implementation in ipairs({"carven", "byte_loop"}) do
                target(case.id .. "-" .. implementation)
                    set_kind("binary")
                    set_default(false)
                    set_group("benchmarks/simd/" .. variant .. "/" .. case.id)
                    add_files("main.cpp", {defines = table.join(workload_defines, {
                        "CARVEN_SIMD_BENCH_CARVEN=" .. (implementation == "carven" and "1" or "0"),
                        "CARVEN_SIMD_BENCH_WORKLOAD=" .. case.id,
                    })})

                    if implementation == "carven" then
                        add_rules("@carven/carven")
                        add_files(case.id .. ".cv")
                    else
                        add_includedirs(path.join(os.projectdir(), "crafts"))
                        add_files("byte_loop.cpp", {defines = workload_defines})
                    end
                target_end()
            end
        end
    end)
end
