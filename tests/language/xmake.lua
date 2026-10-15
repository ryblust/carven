local language_dir = path.join(os.projectdir(), "tests", "language")

local language_feature_dirs = {
    "access",
    "control_flow",
    "failures",
    "functions",
    "patterns",
    "text",
    "types",
}

local language_sources = {}
for _, feature_dir in ipairs(language_feature_dirs) do
    table.insert(language_sources, path.join(language_dir, feature_dir, "*.cv"))
end
table.insert(language_sources, path.join(language_dir, "modules", "**.cv"))
table.insert(language_sources, path.join(language_dir, "testing", "inline.cv"))
table.insert(language_sources, path.join(language_dir, "testing", "assertions.cv"))

local entry_source = path.join(language_dir, "entry", "explicit.cv")
local reporting_source = path.join(language_dir, "testing", "reporting.cv")

target("carven-test-language-time")
    set_default(false)
    add_rules("@carven/carven", {tests = "runner"})
    set_languages("c++20")
    add_files(path.join(language_dir, "time", "*.cv"))
    add_tests("time", {group = "language", run_timeout = 30000})
    on_test(function (target)
        return import("generated", {
            rootdir = path.join(os.projectdir(), "tests", "harness"),
        }).main(target, "Time", "true\ntrue\n")
    end)
target_end()

target("carven-test-language")
    set_default(false)
    add_rules("@carven/carven", {tests = "runner"})

    set_languages("c++20")
    add_includedirs(language_dir)
    for _, source in ipairs(language_sources) do
        add_files(source)
    end
    add_files(path.join(language_dir, "modules", "staged_calls", "provider.cpp"))

    add_files(path.join(language_dir, "main.cv"))
    add_files(path.join(language_dir, "io", "input.cv"))
    add_files(path.join(language_dir, "io", "utf_errors.cv"))
    add_files(path.join(language_dir, "io", "values.cv"))
    for _, source in ipairs({
        "behavior.cv", "core.cv", "containers.cv", "tuples.cv", "tail.cv", "constant.cv", "contracts.cv",
        "owners.cv", "composition.cv", "waiting.cv", "scheduling.cv",
    }) do
        add_files(path.join(language_dir, "async", source))
    end

    add_tests("language", {group = "language", run_timeout = 30000})
    add_tests("input", {group = "language", run_timeout = 30000})
    on_test(function (target, opt)
        local name = opt.name:match("([^/]+)$")
        return import("harness." .. (name == "language" and "behavior" or name), {
            rootdir = language_dir,
        }).main(target)
    end)
target_end()

target("carven-test-language-entry")
    set_default(false)
    add_rules("@carven/carven", {tests = "runner"})

    set_languages("c++20")
    add_includedirs(language_dir)
    add_files(entry_source)

    for _, scenario in ipairs({"success", "throw", "recover", "propagate"}) do
        add_tests(scenario, {group = "language"})
    end
    on_test(function (target, opt)
        import("harness.entry", {rootdir = language_dir}).main(target, opt.name:match("([^/]+)$"))
        return true
    end)
target_end()

for _, scenario in ipairs({
    {name = "failure", status = 1, stderr = {
        ":7:1: error: failure 'tests.language.entry.implicit_failure.Failure' escaped the program entry\n",
        "  failure: Failure {}\n",
        "  note: program exited with a failure status\n",
    }},
    {name = "recovery", status = 0, stderr = {}},
}) do
    target("carven-test-language-implicit-entry-" .. scenario.name)
        set_default(false)
        add_rules("@carven/carven", {tests = "runner"})
        set_languages("c++20")
        add_files(path.join(language_dir, "entry", "implicit_" .. scenario.name .. ".cv"))
        add_tests(scenario.name, {group = "language"})
        on_test(function (target)
            local stdout_file, stderr_file = os.tmpfile(), os.tmpfile()
            local status = os.execv(target:targetfile(), {}, {
                try = true, timeout = 30000, stdout = stdout_file, stderr = stderr_file,
            })
            local stdout = io.readfile(stdout_file) or ""
            local stderr = (io.readfile(stderr_file) or ""):gsub("\r\n", "\n")
            os.tryrm(stdout_file)
            os.tryrm(stderr_file)
            assert(status == scenario.status, "implicit entry exit status: " .. tostring(status))
            assert(stdout == "", "implicit entry emitted unexpected output")
            assert(#scenario.stderr ~= 0 or stderr == "", "implicit entry emitted diagnostics")
            for _, expected in ipairs(scenario.stderr) do
                assert(stderr:find(expected, 1, true),
                    "implicit entry report omits: " .. expected .. "\n" .. stderr)
            end
            return true
        end)
    target_end()
end

target("carven-test-language-reporting")
    set_default(false)
    add_rules("@carven/carven", {tests = "runner"})

    set_languages("c++20")
    add_includedirs(language_dir)
    add_files(reporting_source)
    add_files(path.join(language_dir, "testing", "reporting_driver.cpp"))

    add_tests("reporting", {group = "language"})
    on_test(function (target)
        import("harness.entry", {rootdir = language_dir}).main(target, "reporting")
        return true
    end)
target_end()

target("carven-test-language-runner-failure")
    set_default(false)
    add_rules("@carven/carven", {tests = "main"})
    set_languages("c++20")
    add_files(path.join(language_dir, "testing", "runner_failure.cv"))
    add_tests("failure", {group = "language"})
    on_test(function (target)
        local stdout_file, stderr_file = os.tmpfile(), os.tmpfile()
        local status = os.execv(target:targetfile(), {}, {
            try = true, timeout = 30000, stdout = stdout_file, stderr = stderr_file,
        })
        local stdout = (io.readfile(stdout_file) or ""):gsub("\r\n", "\n")
        local stderr = (io.readfile(stderr_file) or ""):gsub("\r\n", "\n")
        os.tryrm(stdout_file)
        os.tryrm(stderr_file)
        assert(status == 1, "failed tests must produce exit code 1: " .. tostring(status))
        assert(stdout == "ordinary\nafter checks\nlater\n", "unexpected test execution: " .. stdout)
        assert(stderr:find("tests: 2 passed; 3 failed", 1, true), "missing case summary: " .. stderr)
        local _, contexts = stderr:gsub("\n  test:\n", "")
        assert(contexts == 4, "each failure needs its test context: " .. stderr)
        for _, message in ipairs({"first check", "second check", "stop helper", "explicit failure"}) do
            assert(stderr:find(message, 1, true), "missing test failure: " .. message .. "\n" .. stderr)
        end
        return true
    end)
target_end()

-- Printing calls synchronous callable views. Keep its entry outside the async
-- suite, and exercise both the C++20 fallback and C++23 std::print backend.
for _, mode in ipairs({
    {standard = "c++20", suffix = ""},
    {standard = "c++23", suffix = "-cxx23"},
}) do
    target("carven-test-language-printing" .. mode.suffix)
        set_default(false)
        add_rules("@carven/carven")
        set_languages(mode.standard)
        add_files(path.join(language_dir, "printing", "core.cv"))
        add_tests("printing", {group = "language"})
        on_test(function (target)
            return import("harness.printing", {rootdir = language_dir}).main(target)
        end)
    target_end()
end

for _, scenario in ipairs({
    {
        name = "failure",
        ordered = {
            "root failure selected\n",
            "child cancellation requested: true\n",
            "child closed\n",
            "error: failure 'tests.language.async.failure.Failure' escaped the program entry\n",
            "  failure: Failure {}\n",
            "  note: program exited with a failure status\n\n",
        },
        absent = {"cancellation escaped the program entry"},
    },
    {
        name = "cancelled",
        ordered = {
            "root requested child cancellation\n",
            "child accepts cancellation: true\n",
            "error: cancellation escaped the program entry\n",
            "  note: program exited with a failure status\n\n",
        },
        absent = {"unreachable child", "unreachable root", "error: failure '", "  failure:"},
    },
}) do
    target("carven-test-language-async-" .. scenario.name)
        set_default(false)
        add_rules("@carven/carven", {tests = "runner"})
        set_languages("c++20")
        add_files(path.join(language_dir, "async", scenario.name .. ".cv"))
        add_tests("entry", {group = "language", run_timeout = 30000})
        on_test(function (target)
            local stdout_file, stderr_file = os.tmpfile(), os.tmpfile()
            local status = os.execv(target:targetfile(), {}, {
                try = true, timeout = 30000, stdout = stdout_file, stderr = stderr_file,
            })
            local stdout = (io.readfile(stdout_file) or ""):gsub("\r\n", "\n")
            local stderr = (io.readfile(stderr_file) or ""):gsub("\r\n", "\n")
            os.tryrm(stdout_file)
            os.tryrm(stderr_file)
            assert(status == 1, "async entry exit status differs: " .. tostring(status))
            assert(stdout == "", "unexpected async entry stdout: " .. stdout)
            local next_position = 1
            for _, expected in ipairs(scenario.ordered) do
                local first, last = stderr:find(expected, next_position, true)
                assert(first, "async entry report omits or reorders: " .. expected .. "\n" .. stderr)
                next_position = last + 1
            end
            for _, forbidden in ipairs(scenario.absent) do
                assert(not stderr:find(forbidden, 1, true),
                    "unexpected async entry report: " .. forbidden .. "\n" .. stderr)
            end
            return true
        end)
    target_end()
end
