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

for _, mode in ipairs({
    {standard = "c++20", suffix = ""},
    {standard = "c++23", suffix = "-cxx23"},
}) do
    target("carven-test-language" .. mode.suffix)
        set_default(false)
        add_rules("@carven/carven", {tests = "default"})

        set_languages(mode.standard)
        add_includedirs(language_dir)
        add_files(table.unpack(language_sources))

        add_tests("language", {group = "language", run_timeout = 30000})
    target_end()

    target("carven-test-language-entry-point" .. mode.suffix)
        set_default(false)
        add_rules("@carven/carven", {tests = "external"})

        set_languages(mode.standard)
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

    target("carven-test-language-reporting" .. mode.suffix)
        set_default(false)
        add_rules("@carven/carven", {tests = "external"})

        set_languages(mode.standard)
        add_includedirs(language_dir)
        add_files(reporting_source)
        add_files(path.join(language_dir, "testing", "reporting_driver.cpp"))

        add_tests("reporting", {group = "language"})
        on_test(function (target)
            import("harness.entry", {rootdir = language_dir}).main(target, "reporting")
            return true
        end)
    target_end()

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
            assert(stdout == "text: 你好😀\n42\ntrue\n我\n1.25\na\0b\n{unchanged}\nvalue=0007\n9\ndone\nmixed: 42 true 我 1.25 owned\n{value} 7 a\0b \ncount: 3\n1 2 3\n3\nafter 1\n",
                "unexpected stdout: " .. stdout)
            assert(stderr == "error: -3\n\nerror: -4status: false\n", "unexpected stderr: " .. stderr)
            return true
        end)
    target_end()
end
