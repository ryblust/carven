local language_dir = path.join(os.projectdir(), "tests", "language")

local language_feature_dirs = {
    "bindings_and_access",
    "control_flow",
    "functions_and_calls",
    "lambdas_and_callable_views",
    "patterns_and_matches",
    "failure_contracts",
    "types_and_values",
}

local language_sources = {}
for _, feature_dir in ipairs(language_feature_dirs) do
    table.insert(language_sources, path.join(language_dir, feature_dir, "*.cv"))
end
table.insert(language_sources, path.join(language_dir, "modules_and_imports", "**.cv"))
table.insert(language_sources, path.join(language_dir, "testing", "inline.cv"))

local entry_point_source = path.join(language_dir, "testing", "entry_point.cv")
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
        add_files(path.join(language_dir, "testing", "reporting_provider.cpp"))

        add_tests("reporting", {group = "language"})
        on_test(function (target)
            import("harness.entry", {rootdir = language_dir}).main(target, "reporting")
            return true
        end)
    target_end()
end
