for _, variant in ipairs({"native", "portable"}) do
    namespace("benchmarks::json::" .. variant, function ()
        target("validation")
            set_default(false)
            set_kind("binary")
            set_group("benchmarks/json/" .. variant)
            set_languages("c++20")
            set_exceptions("cxx")
            set_optimize("fastest")
            set_policy("build.optimization.lto", false)
            add_defines("NDEBUG")
            if variant == "portable" then
                add_defines("CARVEN_SIMD_FORCE_PORTABLE=1")
            end
            add_rules("carven")
            add_files("main.cpp", "workload.cv")
        target_end()
    end)
end
