local internal_dir = os.scriptdir()
local harness_main = path.join(os.projectdir(), "tests", "harness", "main.cpp")
local crafts_dir = path.join(os.projectdir(), "crafts")

local domains = {
    {name = "analysis", sources = {"semantic/analysis/**.cpp"}},
    {name = "compiler", sources = {"compiler/**.cpp"}},
    {name = "semir", sources = {"semantic/semir/**.cpp", "semantic/visibility/**.cpp"}},
    {name = "backend", sources = {"backend/**.cpp", "artifacts/**.cpp"}},
    {name = "execution", sources = {
        "semantic/evaluation/**.cpp", "semantic/format/**.cpp", "interpreter/**.cpp", "runtime/**.cpp",
    }},
    {name = "foundation", sources = {
        "frontend/**.cpp", "source/**.cpp", "driver/**.cpp", "diagnostics/**.cpp", "support/**.cpp",
        "harness/assertions.cpp",
    }},
}

target("carven-test-internal-fixtures")
    set_default(false)
    set_group("tests/internal")
    set_kind("moduleonly")
    add_deps("carven-modules", "carven-test-support")
    add_includedirs(crafts_dir)
    add_files(path.join(internal_dir, "**.cppm"))
    add_files(path.join(internal_dir, "harness", "death.cpp"))
target_end()

-- Leave headroom for sanitizer instrumentation and shared-runner variability.
-- The timeout guards against stalled tests; it is not a performance threshold.
local internal_test_timeout = has_config("sanitizers") and 180000 or 60000
for _, domain in ipairs(domains) do
    target("carven-test-" .. domain.name)
        set_default(false)
        set_group("tests/internal")
        set_kind("binary")
        add_deps("carven-modules", "carven-test-support", "carven-test-internal-fixtures")
        add_includedirs(crafts_dir)
        add_files(harness_main)
        for _, source in ipairs(domain.sources) do
            add_files(path.join(internal_dir, source))
        end
        add_tests(domain.name, {group = "internal", run_timeout = internal_test_timeout})
    target_end()
end
