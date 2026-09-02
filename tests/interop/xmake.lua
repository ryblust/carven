local interop_dir = path.join(os.projectdir(), "tests", "interop")
local crafts_dir = path.join(os.projectdir(), "crafts")

local function use_local_carven(target)
    import("core.project.project")
    target:values_set("carven.program", project.target("carven"):targetfile())
end

local scalar_boundary_sources = {
    path.join(interop_dir, "scalar_boundary", "scalars.cv"),
    path.join(interop_dir, "scalar_boundary", "api_consumer.cpp"),
}

local provider_form_sources = {
    path.join(interop_dir, "provider_forms", "consumer.cv"),
    path.join(interop_dir, "provider_forms", "header_bridge.cv"),
    path.join(interop_dir, "provider_forms", "linked_provider.cpp"),
}

target("carven-test-interop-scalar-boundary")
    set_default(false)
    add_rules("@carven/carven", {tests = "default"})
    set_values("carven.includedir", crafts_dir)
    set_languages("c++20")
    add_includedirs(interop_dir)
    add_files(table.unpack(scalar_boundary_sources))
    after_load(use_local_carven)
    add_tests("scalar-boundary", {group = "interop"})

target("carven-test-interop-provider-forms")
    set_default(false)
    add_rules("@carven/carven", {tests = "default"})
    set_values("carven.includedir", crafts_dir)
    set_languages("c++20")
    add_includedirs(interop_dir)
    add_files(table.unpack(provider_form_sources))
    after_load(use_local_carven)
    add_tests("provider-forms", {group = "interop"})

target("carven-test-interop-cxx23-compatibility")
    set_default(false)
    add_rules("@carven/carven", {tests = "default"})
    set_values("carven.includedir", crafts_dir)
    set_languages("c++23")
    add_includedirs(interop_dir)
    add_files(table.unpack(scalar_boundary_sources))
    add_files(table.unpack(provider_form_sources))
    after_load(use_local_carven)
    add_tests("cxx23-compatibility", {build_should_pass = true, group = "interop"})

target("carven-test-interop-unicode-export")
    set_default(false)
    add_rules("@carven/carven", {tests = "default"})
    set_values("carven.includedir", crafts_dir)
    set_languages("c++20")
    add_files(
        path.join(interop_dir, "unicode_contract", "export_argument.cv"),
        path.join(interop_dir, "unicode_contract", "export_consumer.cpp")
    )
    after_load(use_local_carven)
    add_tests("unicode-export", {group = "interop"})
    on_test(function (target)
        local stdout_file = os.tmpfile("carven-unicode-export")
        local stderr_file = os.tmpfile("carven-unicode-export")
        local exit_code = os.execv(target:targetfile(), {}, {
            try = true,
            timeout = 30000,
            stdout = stdout_file,
            stderr = stderr_file,
        })
        local stderr = os.isfile(stderr_file) and io.readfile(stderr_file) or ""
        os.tryrm(stdout_file)
        os.tryrm(stderr_file)
        assert(exit_code ~= 0, "invalid export(cpp) Unicode scalar did not terminate")
        assert(
            stderr:find(
                "carven runtime contract error: invalid Unicode scalar at C++ boundary", 1, true
            ),
            "invalid export(cpp) Unicode scalar reported an unexpected diagnostic"
        )
        return true
    end)

target("carven-test-interop-unicode-import")
    set_default(false)
    add_rules("@carven/carven")
    set_values("carven.includedir", crafts_dir)
    set_languages("c++20")
    add_includedirs(interop_dir)
    add_files(
        path.join(interop_dir, "unicode_contract", "import_result.cv"),
        path.join(interop_dir, "unicode_contract", "import_provider.cpp")
    )
    after_load(use_local_carven)
    add_tests("unicode-import", {group = "interop"})
    on_test(function (target)
        local stdout_file = os.tmpfile("carven-unicode-import")
        local stderr_file = os.tmpfile("carven-unicode-import")
        local exit_code = os.execv(target:targetfile(), {}, {
            try = true,
            timeout = 30000,
            stdout = stdout_file,
            stderr = stderr_file,
        })
        local stderr = os.isfile(stderr_file) and io.readfile(stderr_file) or ""
        os.tryrm(stdout_file)
        os.tryrm(stderr_file)
        assert(exit_code ~= 0, "invalid import(cpp) Unicode scalar did not terminate")
        assert(
            stderr:find(
                "carven runtime contract error: invalid Unicode scalar at C++ boundary", 1, true
            ),
            "invalid import(cpp) Unicode scalar reported an unexpected diagnostic"
        )
        return true
    end)
