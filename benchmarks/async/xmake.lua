includes("workloads.lua")
local workload_cases = cases()

option("libcoro")
    set_default("")
    set_showmenu(true)
    set_description("Optional libcoro checkout for async benchmarks")
option_end()

namespace("benchmarks::async", function ()
    set_languages("c++20")
    set_exceptions("cxx")

    add_includedirs(".", path.join(os.projectdir(), "crafts"))

    local function configure_profile(profile)
        set_optimize(profile == "stack" and "none" or "fastest")
        if profile == "stack" then
            add_cxxflags("-fno-optimize-sibling-calls")
        else
            add_defines("NDEBUG")
        end
    end

    local implementations = {"carven", "cpp_task", "carven_runtime"}
    local libcoro = get_config("libcoro")
    if libcoro and #libcoro > 0 then
        libcoro = path.absolute(libcoro, os.projectdir())
        table.insert(implementations, "libcoro")
    end

    for _, profile in ipairs({"timing", "allocations", "stack"}) do
        local profile_defines = {
            "CARVEN_ASYNC_BENCH_ALLOCATIONS=" .. (profile == "allocations" and "1" or "0"),
            "CARVEN_ASYNC_BENCH_STACK=" .. (profile == "stack" and "1" or "0"),
        }
        target("measurement-" .. profile)
            set_default(false)
            set_kind("object")
            configure_profile(profile)
            add_files("measurement.cpp", {defines = profile_defines})
        target_end()

        for _, case in ipairs(workload_cases) do
            if profile ~= "stack" or case.chain then
                for _, implementation in ipairs(implementations) do
                    target(case.id .. "-" .. implementation .. "-" .. profile)
                        set_kind("binary")
                        set_default(false)
                        set_group("benchmarks/async/" .. case.id)
                        configure_profile(profile)
                        add_deps("measurement-" .. profile)

                        if implementation == "carven" then
                            add_rules("carven")
                            add_files(case.id .. ".cv", {defines = profile_defines})
                        else
                            local native_defines = table.join(profile_defines, defines(case), {
                                "CARVEN_ASYNC_BENCH_LIBCORO=" .. (implementation == "libcoro" and "1" or "0"),
                            })
                            if implementation == "carven_runtime" then
                                add_files("runtime_workload.cpp", {defines = native_defines})
                            else
                                add_files("workload.cpp", {defines = native_defines})
                            end

                            if implementation == "libcoro" then
                                add_includedirs(path.join(libcoro, "include"))
                            end
                        end
                    target_end()
                end
            end
        end
    end
end)
