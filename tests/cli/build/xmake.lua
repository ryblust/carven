local build_dir = os.scriptdir()

target("carven-test-cli-build")
    set_default(false)
    set_kind("phony")
    set_group("cli")
    add_deps("carven", {inherit = false})
    add_tests("objects", {group = "cli", run_timeout = 30000})
    on_test(function (target, opt)
        local process = import("process", {rootdir = path.join(os.projectdir(), "tests", "harness")})
        local cxx = assert(import("lib.detect.find_tool")("clang++"))
        return process.main(target:dep("carven"), opt, {
            fixtures = {["objects.cv"] = "crafts/carven/tests/build.cv"},
            args = {"interpret", "--tests", "crafts/carven/tests/build.cv"},
            envs = {CARVEN_TEST_CXX = cxx.program},
            stderr_contains = {"tests: 2 passed; 0 failed"},
        }, build_dir)
    end)
target_end()
