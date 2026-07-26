local interop_dir = path.join(os.projectdir(), "tests", "interop")
local crafts_dir = path.join(os.projectdir(), "crafts")
local interop_source = path.join(interop_dir, "cases", "inline_cpp.cv")

local function use_local_carven(target)
    import("core.project.project")
    target:values_set("carven.program", project.target("carven"):targetfile())
end

local consumer_standards = {
    {name = "cxx20", language = "c++20"},
    {name = "cxx23", language = "c++23"},
}

for _, standard in ipairs(consumer_standards) do
    target("carven-test-interop-" .. standard.name, function ()
        set_default(false)
        add_packages("carven")
        add_rules("@carven/carven", {tests = "default"})
        set_values("carven.includedir", crafts_dir)
        set_languages(standard.language)
        add_files(interop_source)
        after_load(use_local_carven)
        add_tests(standard.name, {realtime_output = false, group = "interop"})
    end)
end

target("carven-test-interop-invalid-unicode")
    set_default(false)
    add_packages("carven")
    add_rules("@carven/carven")
    set_values("carven.includedir", crafts_dir)
    set_languages("c++20")
    add_files(path.join(interop_dir, "cases", "invalid_unicode.cv"))
    after_load(use_local_carven)
    add_tests("invalid-unicode", {group = "interop"})
    on_test(function (target)
        local failure
        try {
            function ()
                os.iorunv(target:targetfile(), {}, {timeout = 30000})
            end,
            catch {
                function (errors)
                    failure = errors
                end,
            },
        }
        assert(failure, "invalid typed #[cpp] Unicode did not terminate")
        assert(
            failure.stderr:find(
                "carven runtime contract error: invalid UTF-8 from typed #[cpp]", 1, true
            ),
            "invalid typed #[cpp] Unicode reported an unexpected diagnostic"
        )
        return true
    end)

local linkage_source = path.join(interop_dir, "cases", "linkage_domain.cv")

local linkage_domains = {
    {name = "left"},
    {name = "right"},
    {name = "override", domain = "carven-tests:explicit-linkage-domain"},
}

for _, domain in ipairs(linkage_domains) do
    target("carven-test-linkage-domain-" .. domain.name, function ()
        set_default(false)
        set_kind("object")
        add_packages("carven")
        add_rules("@carven/carven", {linkage_domain = domain.domain})
        set_values("carven.includedir", crafts_dir)
        set_languages("c++20")
        add_files(linkage_source)
        after_load(use_local_carven)
    end)
end

target("carven-test-interop-linkage-domains")
    set_default(false)
    set_kind("binary")
    set_languages("c++20")
    add_deps(
        "carven-test-linkage-domain-left",
        "carven-test-linkage-domain-right",
        "carven-test-linkage-domain-override"
    )
    add_files(path.join(interop_dir, "cases", "linkage_domain_main.cpp"))
    add_tests("linkage-domains", {realtime_output = false, group = "interop"})
