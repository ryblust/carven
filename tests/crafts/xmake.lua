-- SIMD APIs, scanners, and UTF/JSON validation also run with the portable backend.
for _, variant in ipairs({
    {name = "carven-test-crafts", test = "crafts"},
    {name = "carven-test-crafts-portable", test = "crafts-portable", portable = true},
}) do
    target(variant.name)
        set_default(false)
        set_languages("c++20")
        if variant.portable then
            add_defines("CARVEN_SIMD_FORCE_PORTABLE")
        else
            add_vectorexts("avx2")
        end
        add_rules("@carven/carven", {tests = variant.portable and "main" or "runner"})
        if variant.portable then
            add_files("carven/std/utf/cases.cv", "carven/std/utf/blocks.cv", "carven/std/json/runs.cv")
            add_files("carven/std/simd/bytes.cv", "carven/std/simd/floats.cv",
                "carven/std/simd/primitives.cv", "carven/std/simd/scan.cv")
        else
            add_files("carven/**.cv", "main.cv")
        end
        add_tests(variant.test, {group = "crafts", run_timeout = 30000})
        on_test(function (target)
            return import("generated", {
                rootdir = path.join(os.projectdir(), "tests", "harness"),
            }).main(target, "Crafts")
        end)
    target_end()
end
