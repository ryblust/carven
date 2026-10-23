local internal_dir = os.scriptdir()

target("carven-test-fixtures")
    set_default(false)
    set_group("tests/internal")
    set_kind("moduleonly")
    add_deps("carven-test-support", "carven-hosting")
    add_files(path.join(internal_dir, "**.cppm"))
target_end()

-- Leave headroom for sanitizer instrumentation and shared-runner variability.
-- The timeout guards against stalled tests; it is not a performance threshold.
local internal_test_timeout = has_config("sanitizers") and 180000 or 60000

target("carven-test-internal")
    set_default(false)
    set_group("tests/internal")
    set_kind("binary")
    add_deps("carven-test-fixtures", "carven-test-runner")
    add_files(path.join(internal_dir, "**.cpp"))
    add_files(path.join(os.projectdir(), "crafts/carven/std/net/native.cpp"))
    if is_plat("windows", "mingw") then
        add_syslinks("ws2_32")
    end
    local shards = 6
    for index = 1, shards do
        add_tests("shard-" .. index, {
            group = "internal", run_timeout = internal_test_timeout,
            runargs = {"--shard", index .. "/" .. shards},
        })
    end
target_end()
