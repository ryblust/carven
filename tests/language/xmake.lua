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

local entry_point_source = path.join(language_dir, "entry", "entry_point.cv")
local reporting_source = path.join(language_dir, "testing", "reporting.cv")

target("carven-test-language")
    set_default(false)
    add_rules("@carven/carven", {tests = "default"})

    set_languages("c++20")
    add_includedirs(language_dir)
    add_files(table.unpack(language_sources))

    add_tests("language", {group = "language", run_timeout = 30000})
target_end()

target("carven-test-language-entry-point")
    set_default(false)
    add_rules("@carven/carven", {tests = "external"})

    set_languages("c++20")
    add_includedirs(language_dir)
    add_files(entry_point_source)

    for _, scenario in ipairs({"success", "throw", "recover", "propagate"}) do
        add_tests(scenario, {group = "language"})
    end
    on_test(function (target, opt)
        import("harness.entry", {rootdir = language_dir}).main(target, opt.name:match("([^/]+)$"))
        return true
    end)
target_end()

target("carven-test-language-reporting")
    set_default(false)
    add_rules("@carven/carven", {tests = "external"})

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
    add_rules("@carven/carven", {tests = "default"})
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
        for _, message in ipairs({"first check", "second check", "stop helper", "explicit failure"}) do
            assert(stderr:find(message, 1, true), "missing test failure: " .. message .. "\n" .. stderr)
        end
        return true
    end)
target_end()

for _, mode in ipairs({
    {standard = "c++20", suffix = ""},
    {standard = "c++23", suffix = "-cxx23"},
}) do
    target("carven-test-language-printing" .. mode.suffix)
        set_default(false)
        add_rules("@carven/carven")
        set_languages(mode.standard)
        add_files(path.join(language_dir, "printing", "printing.cv"))
        add_tests("printing", {group = "language"})
        on_test(function (target)
            local stdout_file, stderr_file = os.tmpfile(), os.tmpfile()
            local status = os.execv(target:targetfile(), {}, {
                try = true, timeout = 30000, stdout = stdout_file, stderr = stderr_file,
            })
            -- NUL bytes require explicit binary reads instead of encoding detection.
            local stdout = io.readfile(stdout_file, {encoding = "binary"})
            local stderr = io.readfile(stderr_file, {encoding = "binary"})
            os.tryrm(stdout_file)
            os.tryrm(stderr_file)
            assert(status == 0, "printing failed: " .. tostring(status))
            assert(stdout == "text: 你好😀\n42\ntrue\n我\n1.25\na\0b\n{unchanged}\nvalue=0007\n9\ndone\nmixed: 42 true 我 1.25 owned\n{value} 7 a\0b \ncount: 3\n1 2 3\n3\nafter 1\n1 false 11\n11\n42 before 1\n42 after 1\n18446744073709551615 -9223372036854775808\n"
                .. 'PrintedOrder {\n    price: PrintedMoney {\n        cents: 1250,\n    },\n    names: [\n        "a",\n        "b\\n",\n    ],\n}\n'
                .. 'PrintedState::Done(\n    42,\n    "ok",\n) PrintedState::Pending PrintedCode::Bad\n'
                .. 'PrintedEmpty {} [\n    1,\n    2,\n    3,\n]\n[\n    4,\n    5,\n]\n'
                .. 'PrintedOrder {\n    price: PrintedMoney {\n        cents: 99,\n    },\n    names: [\n        "a",\n        "b\\n",\n    ],\n} 1\n'
                .. 'PrintedOrder {\n    price: PrintedMoney {\n        cents: 99,\n    },\n    names: [\n        "a",\n        "b\\n",\n    ],\n}\n1..=3\n',
                "unexpected stdout: " .. stdout)
            assert(stderr == "error: -3\n\nerror: -4status: false\n", "unexpected stderr: " .. stderr)
            return true
        end)
    target_end()
end
