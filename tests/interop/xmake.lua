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

local interop_sources = table.join(scalar_boundary_sources, provider_form_sources)
table.join2(interop_sources, os.files(path.join(interop_dir, "runtime_headers", "*.cpp")))
table.join2(interop_sources, os.files(path.join(interop_dir, "unicode_contract", "export_argument.cv")))
table.join2(interop_sources, os.files(path.join(interop_dir, "cpp_names", "*.cv")))
table.join2(interop_sources, os.files(path.join(interop_dir, "cpp_names", "interface.cpp")))
table.join2(interop_sources, os.files(path.join(interop_dir, "cpp_names", "escaped_api.cpp")))
table.join2(interop_sources, os.files(path.join(interop_dir, "cpp_names", "global_interface.cpp")))
table.join2(interop_sources, os.files(path.join(interop_dir, "discarded", "operations.cv")))
table.join2(interop_sources, os.files(path.join(interop_dir, "harness", "runner.cpp")))

target("carven-test-interop")
    set_default(false)
    add_rules("@carven/carven", {tests = "external"})
    set_values("carven.includedir", crafts_dir)
    set_languages("c++20")
    add_includedirs(interop_dir)
    add_files(table.unpack(interop_sources))
    after_load(use_local_carven)
    add_tests("interop", {group = "interop"})
    on_test(function (target)
        local check_process = import("harness.process", {rootdir = interop_dir})
        check_process(target, {}, 0, "interop behavior checks")
        for _, operation in ipairs({"divide", "remainder", "shift", "width", "index", "unicode", "unicode-export"}) do
            check_process(target, {operation}, 73, operation .. " runtime check")
        end
        return true
    end)

target("carven-test-interop-exception-boundary")
    set_default(false)
    set_kind("binary")
    set_languages("c++20")
    set_exceptions("cxx")
    add_includedirs(crafts_dir)
    add_files(path.join(interop_dir, "exception_boundary", "terminate.cpp"))
    add_tests("exception-boundary", {group = "interop"})
    on_test(function (target)
        local check_process = import("harness.process", {rootdir = interop_dir})
        for _, operation in ipairs({"copy", "move", "failure", "function", "object"}) do
            check_process(target, {operation}, 73, operation .. " termination check")
        end
        return true
    end)

target("carven-test-interop-cxx23")
    set_default(false)
    add_rules("@carven/carven", {tests = "external"})
    set_values("carven.includedir", crafts_dir)
    set_languages("c++23")
    add_includedirs(interop_dir)
    add_files(table.unpack(interop_sources))
    after_load(use_local_carven)
    add_files(path.join(interop_dir, "output", "print.cv"))
    add_tests("cxx23", {group = "interop"})
    on_test(function (target)
        local output, errors = os.iorunv(target:targetfile(), {}, {timeout = 30000})
        assert(output:gsub("\r\n", "\n") == "Literal\nValue: {123}\n" and errors == "",
            "C++23 interop produced unexpected output:\n%s\n%s", output, errors)
        return true
    end)
