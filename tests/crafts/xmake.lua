-- SIMD APIs, scanners, and UTF/JSON validation also run with the portable backend.
for _, variant in ipairs({
    {name = "carven-test-crafts"},
    {name = "carven-test-crafts-portable", portable = true},
}) do
    target(variant.name)
        set_default(false)
        set_kind("binary")
        set_group("crafts")
        set_languages("c++20")
        if variant.portable then
            add_defines("CARVEN_SIMD_FORCE_PORTABLE")
        else
            add_vectorexts("avx2")
        end
        add_rules("carven", {tests = true})
        add_tests("default", {run_timeout = 30000})
        if variant.portable then
            add_files("carven/std/utf/cases.cv", "carven/std/utf/blocks.cv",
                "carven/std/utf/text.cv", "carven/std/json/runs.cv")
            add_files("carven/std/simd/bytes.cv", "carven/std/simd/floats.cv",
                "carven/std/simd/primitives.cv", "carven/std/simd/scan.cv")
        else
            add_files("carven/**.cv", "fs/bridge.cv")
        end
    target_end()
end

target("carven-test-native-async")
    set_default(false)
    set_group("crafts")
    set_kind("binary")
    set_languages("c++20")
    add_rules("carven", {tests = true})
    add_files("async/operations.cv")
    add_tests("operations", {run_timeout = 30000})
target_end()

-- The public process contract uses the same source as its hosted consumer.
target("carven-test-native-process")
    set_default(false)
    set_group("crafts")
    set_kind("binary")
    set_languages("c++20")
    add_rules("carven")
    add_files("process/bridge.cv")
    add_tests("roundtrip", {run_timeout = 30000})
    on_test(function (target, opt)
        local child = path.absolute(target:targetfile())
        local runenvs = import("private.action.run.runenvs")
        local addenvs, setenvs = runenvs.make(target)
        local status, errors = os.execv(target:targetfile(), {child, "", "我😀"}, {
            envs = runenvs.join(addenvs, setenvs), timeout = 15000, try = true,
        })
        if status ~= 0 then
            opt.errors = errors or string.format("native process exited with status %s", status)
        end
        return status == 0
    end)
target_end()
