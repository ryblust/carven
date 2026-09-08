local language_dir = path.join(os.projectdir(), "tests", "language")
local crafts_dir = path.join(os.projectdir(), "crafts")

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
    table.join2(language_sources, os.files(path.join(language_dir, feature_dir, "*.cv")))
end
table.join2(language_sources, os.files(path.join(language_dir, "modules_and_imports", "**.cv")))
table.insert(language_sources, path.join(language_dir, "testing", "inline.cv"))
table.sort(language_sources)

local function use_local_carven(target)
    import("core.project.project")
    target:values_set("carven.program", project.target("carven"):targetfile())
end

target("carven-test-language")
    set_default(false)
    add_rules("@carven/carven", {tests = "default"})
    set_values("carven.includedir", crafts_dir)
    set_languages("c++20")
    add_includedirs(language_dir)
    add_files(table.unpack(language_sources))
    after_load(use_local_carven)
    add_tests("language", {group = "language"})

target("carven-test-language-cxx23-compatibility")
    set_default(false)
    add_rules("@carven/carven", {tests = "external"})
    set_values("carven.includedir", crafts_dir)
    set_languages("c++23")
    add_includedirs(language_dir)
    add_files(table.unpack(language_sources))
    add_files(path.join(language_dir, "testing", "entry_point.cv"))
    after_load(use_local_carven)
    add_tests("cxx23-compatibility", {build_should_pass = true, group = "language"})

local entry_point_source = path.join(language_dir, "testing", "entry_point.cv")

target("carven-test-language-entry-point")
    set_default(false)
    add_rules("@carven/carven", {tests = "external"})
    set_values("carven.includedir", crafts_dir)
    set_languages("c++20")
    add_includedirs(language_dir)
    add_files(entry_point_source)
    after_load(use_local_carven)
    add_tests("entry-point", {group = "language"})
    on_test(function (target)
        import("harness.entry", {rootdir = language_dir}).main(target, "entry-point")
        return true
    end)

local reporting_source = path.join(language_dir, "testing", "reporting.cv")

target("carven-test-language-reporting")
    set_default(false)
    add_rules("@carven/carven", {tests = "external"})
    set_values("carven.includedir", crafts_dir)
    set_languages("c++20")
    add_includedirs(language_dir)
    add_files(reporting_source)
    add_files(path.join(language_dir, "testing", "reporting_provider.cpp"))
    after_load(use_local_carven)
    add_tests("reporting", {group = "language"})
    on_test(function (target)
        import("harness.entry", {rootdir = language_dir}).main(target, "reporting")
        return true
    end)
