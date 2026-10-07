-- SIMD scanners and UTF/JSON validation also run with the portable backend.
for _, variant in ipairs({
    {name = "carven-test-crafts", test = "crafts"},
    {name = "carven-test-crafts-portable", test = "crafts-portable", portable = true},
}) do
    target(variant.name)
        set_default(false)
        set_languages("c++20")
        if variant.portable then
            add_defines("CARVEN_SIMD_FORCE_PORTABLE")
        end
        add_rules("@carven/carven", {tests = "default"})
        if variant.portable then
            add_files("carven/std/utf/blocks.cv", "carven/std/utf/validation.cv",
                "carven/std/simd/**.cv", "carven/std/json/**.cv")
        else
            add_files("carven/**.cv|std/time/sleep.cv|std/time/timeout.cv")
        end
        add_tests(variant.test, {group = "crafts", run_timeout = 30000})
        on_test(function (target)
            return import("generated", {
                rootdir = path.join(os.projectdir(), "tests", "harness"),
            }).main(target, "Crafts")
        end)
    target_end()
end

for _, scenario in ipairs({"sleep", "timeout"}) do
    target("carven-test-crafts-time-" .. scenario)
        set_default(false)
        add_rules("@carven/carven", {tests = "external"})
        set_languages("c++20")
        add_files("carven/std/time/" .. scenario .. ".cv")
        add_tests("behavior", {group = "crafts", run_timeout = 30000})
        on_test(function (target)
            local output, errors = os.iorunv(target:targetfile(), {}, {timeout = 30000})
            assert(output:gsub("\r\n", "\n") == "true\n", "time contracts differ: " .. output)
            assert(errors == "", "unexpected time entry report: " .. errors)
            return true
        end)
    target_end()
end
