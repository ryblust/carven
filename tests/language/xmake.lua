local language_dir = os.scriptdir()

set_default(false)
set_group("language")
set_languages("c++20")

target("carven-test-language")
    set_kind("binary")
    add_rules("@carven/carven", {tests = true})
    add_tests("default", {run_timeout = 30000})
    add_includedirs(".")
    for _, directory in ipairs({
        "access", "control_flow", "failures", "functions", "patterns", "text", "types",
    }) do
        add_files(directory .. "/*.cv")
    end
    add_files("modules/**.cv", "modules/staged_calls/provider.cpp")
    add_files("testing/inline.cv", "testing/assertions.cv", "io/values.cv", "time/clock.cv")
target_end()

includes("traps")

target("carven-test-language-execution")
    set_kind("binary")
    add_rules("@carven/carven")
    add_includedirs(".")
    add_files("main.cv", "io/input.cv", "io/utf_errors.cv", "time/sleep.cv", "time/timeout.cv")
    add_files("async/*.cv")

    add_tests("behavior", {run_timeout = 30000, plain = true, pass_outputs = {""}})
    for _, name in ipairs({"io-error", "partial-cancel", "fragmented-crlf", "fragmented-utf8"}) do
        add_tests("input-" .. name, {
            run_timeout = 10000, runargs = {name, "世界", ""},
            plain = true, pass_outputs = {"ok\n", "ok\r\n"},
        })
    end
    after_test(function (target, opt)
        assert(not opt.stderr or opt.stderr == "", opt.stderr)
    end)
target_end()

target("carven-test-language-input")
    set_kind("phony")
    add_deps("carven-test-language-execution")
    for _, name in ipairs({
        "lines", "empty", "bytes", "text", "limit", "line-limit", "invalid", "invalid-text", "utf-errors", "cold-fifo",
    }) do
        add_tests(name, {run_timeout = 10000})
    end
    on_test(function (target, opt)
        local name = opt.name:match("([^/]+)$")
        return import("harness.input", {rootdir = language_dir})
            .main(target:dep("carven-test-language-execution"), name)
    end)
target_end()

target("carven-test-language-entry")
    add_rules("@carven/carven", {tests = {entry = false}})

    add_includedirs(".")
    add_files("entry/explicit.cv")

    for _, name in ipairs({"success", "recover", "throw", "propagate"}) do
        add_tests(name, {run_timeout = 30000})
    end
    on_test(function (target, opt)
        local name = opt.name:match("([^/]+)$")
        local failure = name == "throw" or name == "propagate"
        return import("process", {rootdir = path.join(os.projectdir(), "tests", "harness")})
            .main(target, opt, {
                args = {name, "alpha", "beta"}, exit_code = failure and 1 or 0,
                stderr_contains = failure and {"tests: 1 passed; 0 failed", "escaped the program entry", "payload: <opaque>"}
                    or {"tests: 1 passed; 0 failed"},
            }, target:scriptdir())
    end)
target_end()

target("carven-test-language-implicit-entry")
    add_rules("@carven/carven")
    add_files("entry/implicit_recovery.cv")
    add_tests("recovery", {run_timeout = 30000})
target_end()

target("carven-test-language-printing")
    set_languages("c++20")
    add_rules("@carven/carven")
    add_files("printing/core.cv")
    add_tests("printing", {run_timeout = 30000})
    on_test(function (target, opt)
        return import("process", {rootdir = path.join(os.projectdir(), "tests", "harness")})
            .main(target, opt, {
                args = {}, stdout = "stdout.txt", stderr = "stderr.txt",
            }, path.join(language_dir, "printing"))
    end)
target_end()

for _, case in ipairs({
    {name = "implicit-failure", source = "implicit_failure.cv", reports = {"escaped the program entry", "Failure {}"}},
    {name = "async-failure", source = "async_failure.cv", reports = {"root failure selected", "child cancellation requested: true", "child closed", "escaped the program entry"}},
    {name = "async-cancelled", source = "async_cancelled.cv", reports = {"root requested child cancellation", "child accepts cancellation: true", "cancellation escaped the program entry"}},
}) do
    target("carven-test-language-entry-" .. case.name)
        add_rules("@carven/carven")
        add_files("entry/" .. case.source)
        add_tests("entry", {run_timeout = 30000})
        on_test(function (target, opt)
            return import("process", {rootdir = path.join(os.projectdir(), "tests", "harness")})
                .main(target, opt, {
                    args = {}, exit_code = 1, stderr_ordered = case.reports,
                    stderr_not_contains = {"unreachable"},
                }, target:scriptdir())
        end)
    target_end()
end

target("carven-test-language-test-control")
    set_kind("binary")
    add_rules("@carven/carven", {tests = true})
    add_includedirs(".")
    add_files("testing/reporting.cv", "testing/runner_failure.cv")
    add_files("../cli/commands/assertions/loop_step_stop.cv", "../cli/commands/assertions/evaluation_stop.cv")
    add_tests("control", {run_timeout = 30000})
    on_test(function (target, opt)
        return import("process", {rootdir = path.join(os.projectdir(), "tests", "harness")})
            .main(target, opt, {
                args = {}, exit_code = 1,
                stdout_ordered = {"reached 0", "reached 1", "reached 2", "reached 3", "reached 4",
                    "reached 6", "reached 8", "closed 10", "closed 12", "reached 17", "closed 19", "closed 21"},
                stdout_contains = {"ordinary", "after checks", "later", "body"},
                stdout_not_contains = {"unreachable", "unexpected recovery", "program entry",
                    "reached 5\n", "reached 7\n", "reached 9\n", "reached 11\n", "reached 13\n",
                    "reached 14\n", "reached 15\n", "reached 16\n", "reached 18\n", "reached 20\n",
                    "reached 22\n", "reached 23\n"},
                stderr_contains = {"test stopped", "step stopped", "evaluation stopped", "first check", "second check",
                    "direct temporary message with enough text for owned storage",
                    "explicit temporary message with enough text for owned storage",
                    "callable temporary message with enough text for owned storage",
                    "actual: [\n        12,\n        13,", "expected: [\n        15,\n        16,",
                    "observed_boolean(&calls, true): <not evaluated>", "comparison_next(&calls): 1", "comparison_next(&calls): 2"},
                stderr_not_contains = {"99,", "unexpected recovery"},
            }, target:scriptdir())
    end)
target_end()
