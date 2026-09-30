-- UTF validation also runs with the forced portable backend.
for _, variant in ipairs({
    {name = "carven-test-crafts", test = "crafts"},
    {name = "carven-test-crafts-scalar", test = "crafts-scalar", scalar = true},
}) do
    target(variant.name)
        set_default(false)
        set_languages("c++20")
        if variant.scalar then
            add_defines("CARVEN_SIMD_FORCE_SCALAR")
        end
        add_rules("@carven/carven", {tests = "default"})
        if variant.scalar then
            add_files("carven/std/utf/blocks.cv", "carven/std/utf/validation.cv")
        else
            add_files("carven/**.cv")
        end
        add_tests(variant.test, {group = "crafts", run_timeout = 30000})
        on_test(function (target)
            return import("generated", {
                rootdir = path.join(os.projectdir(), "tests", "harness"),
            }).main(target, "Crafts")
        end)
    target_end()
end
