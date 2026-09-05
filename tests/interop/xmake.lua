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

local runtime_header_sources = {
    path.join(interop_dir, "runtime_headers", "passing.cpp"),
    path.join(interop_dir, "runtime_headers", "numeric.cpp"),
    path.join(interop_dir, "runtime_headers", "array.cpp"),
    path.join(interop_dir, "runtime_headers", "text.cpp"),
    path.join(interop_dir, "runtime_headers", "entry.cpp"),
    path.join(interop_dir, "runtime_headers", "callable.cpp"),
    path.join(interop_dir, "runtime_headers", "outcome.cpp"),
    path.join(interop_dir, "runtime_headers", "unreachable.cpp"),
    path.join(interop_dir, "runtime_headers", "runtime.cpp"),
    path.join(interop_dir, "runtime_headers", "testing.cpp"),
}

local unicode_export_sources = {
    path.join(interop_dir, "unicode_contract", "export_argument.cv"),
    path.join(interop_dir, "unicode_contract", "export_consumer.cpp"),
}

local function unicode_contract_test(name, boundary)
    return function (target)
        local stdout_file = os.tmpfile("carven-unicode-" .. name)
        local stderr_file = os.tmpfile("carven-unicode-" .. name)
        local exit_code = os.execv(target:targetfile(), {}, {
            try = true,
            timeout = 30000,
            stdout = stdout_file,
            stderr = stderr_file,
        })
        local stderr = os.isfile(stderr_file) and io.readfile(stderr_file) or ""
        os.tryrm(stdout_file)
        os.tryrm(stderr_file)
        assert(exit_code ~= 0, "invalid " .. boundary .. " Unicode scalar did not terminate")
        assert(
            stderr:find(
                "carven runtime contract error: invalid Unicode scalar at C++ boundary", 1, true
            ),
            "invalid " .. boundary .. " Unicode scalar reported an unexpected diagnostic"
        )
        return true
    end
end

target("carven-test-interop-runtime-headers")
    set_default(false)
    set_kind("static")
    set_languages("c++20")
    add_includedirs(crafts_dir)
    add_files(table.unpack(runtime_header_sources))
    add_tests("runtime-headers", {build_should_pass = true, group = "interop"})

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

target("carven-test-interop-unicode-export")
    set_default(false)
    add_rules("@carven/carven", {tests = "default"})
    set_values("carven.includedir", crafts_dir)
    set_languages("c++20")
    add_files(table.unpack(unicode_export_sources))
    after_load(use_local_carven)
    add_tests("unicode-export", {group = "interop"})
    on_test(unicode_contract_test("export", "export(cpp)"))

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
    on_test(unicode_contract_test("import", "import(cpp)"))

target("carven-test-interop-exception-boundary")
    set_default(false)
    set_kind("binary")
    set_languages("c++20")
    set_exceptions("cxx")
    add_includedirs(crafts_dir)
    add_files(path.join(interop_dir, "exception_boundary", "terminate.cpp"))
    add_tests("exception-boundary", {group = "interop"})
    on_test(function (target)
        for _, operation in ipairs({"copy", "move", "failure", "function", "object"}) do
            local code = os.execv(target:targetfile(), {operation}, {try = true, timeout = 30000})
            assert(code == 73, operation .. " did not reach std::terminate")
        end
        return true
    end)
