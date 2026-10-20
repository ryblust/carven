local build_dir = os.scriptdir()

target("carven-test-cli-build")
    set_default(false)
    set_kind("phony")
    set_group("cli")
    add_deps("carven", {inherit = false})
    if not is_plat("windows", "mingw") then
        add_tests("toolchain", {group = "cli", run_timeout = 120000})
    end
    add_tests("declarative", {group = "cli", run_timeout = 120000})
    add_tests("imperative", {group = "cli", run_timeout = 120000})
    add_tests("objects", {group = "cli", run_timeout = 30000})
    on_test(function (target, opt)
        local name = opt.name:match("^[^/]+/(.+)$")
        if name == "toolchain" then
            local python = assert(import("lib.detect.find_tool")("python3"))
            local runenvs = import("private.action.run.runenvs")
            local additions, overrides = runenvs.make(target:dep("carven"))
            os.vrunv(python.program, {
                path.join(build_dir, "cli.py"),
                path.absolute(target:dep("carven"):targetfile()),
                path.join(os.projectdir(), "crafts"),
            }, {envs = runenvs.join(additions, overrides), timeout = 120000})
            return true
        end
        local process = import("process", {rootdir = path.join(os.projectdir(), "tests", "harness")})
        if name == "objects" then
            local cxx = assert(import("lib.detect.find_tool")("clang++"))
            return process.main(target:dep("carven"), opt, {
                fixtures = {["objects.cv"] = "crafts/carven/tests/build.cv"},
                args = {"interpret", "--tests", "crafts/carven/tests/build.cv"},
                envs = {CARVEN_TEST_CXX = cxx.program},
                stderr_contains = {"tests: 1 passed; 0 failed"},
            }, build_dir)
        end
        local suffix = is_plat("windows", "mingw") and ".exe" or ""
        if name == "imperative" then
            return process.main(target:dep("carven"), opt, {
                inputs = {"build.cv", "settings.toml", "src/main.cv", "src/check.cv", "src/catalog.cv"},
                steps = {
                    {args = {"build"}, run_timeout = 120000,
                        stdout_contains = {"Ready stock-report:", "Ready inventory-check:", "Checked 2 catalog items"},
                        stderr_contains = {},
                        output_files = {"build/stock-report" .. suffix}},
                    {program = "build/stock-report" .. suffix, args = {},
                        stdout_contains = {"pencil: 30 in stock (minimum 20)\n"},
                        stdout_not_contains = {"notebook:"}},
                },
            }, path.join(os.projectdir(), "examples", "build", "imperative"))
        end
        return process.main(target:dep("carven"), opt, {
            inputs = {"build.cv", "src/main.cv", "src/check.cv", "src/catalog.cv"},
            steps = {
                {args = {"run", "inventory-check"}, run_timeout = 120000,
                    stdout_contains = {"Checked 2 catalog items"}, stderr_contains = {},
                    output_files = {"build/inventory" .. suffix, "build/inventory-check" .. suffix}},
                {program = "build/inventory" .. suffix, args = {},
                    stdout_contains = {"notebook: 12 in stock", "pencil: 30 in stock"}},
            },
        }, path.join(os.projectdir(), "examples", "build", "declarative"))
    end)
target_end()
