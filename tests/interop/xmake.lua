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
table.join2(interop_sources, os.files(path.join(interop_dir, "interpolation", "*.cv")))
table.join2(interop_sources, os.files(path.join(interop_dir, "pointers", "*.cv")))
table.join2(interop_sources, os.files(path.join(interop_dir, "pointers", "interface.cpp")))
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
    add_tests("behavior", {group = "interop"})
    for _, operation in ipairs({"divide", "remainder", "shift", "width", "index", "unicode", "unicode-export"}) do
        add_tests(operation, {group = "interop"})
    end
    on_test(function (target, opt)
        local name = opt.name:match("([^/]+)$")
        local arguments = name == "behavior" and {} or {name}
        import("harness.process", {rootdir = interop_dir})(target, arguments,
            name == "behavior" and 0 or 73, name)
        return true
    end)

target("carven-test-interop-exception-boundary")
    set_default(false)
    set_kind("binary")
    set_languages("c++20")
    set_exceptions("cxx")
    add_includedirs(crafts_dir)
    add_files(path.join(interop_dir, "exception_boundary", "terminate.cpp"))
    for _, operation in ipairs({
        "copy", "move", "failure", "function", "object",
        "string-allocate", "string-copy",
        "format-width", "format-throw", "format-utf8", "format-allocate"
    }) do
        add_tests(operation, {group = "interop"})
    end
    on_test(function (target, opt)
        local operation = opt.name:match("([^/]+)$")
        import("harness.process", {rootdir = interop_dir})(target, {operation}, 73, operation)
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
    add_tests("compatibility", {group = "interop"})

target("carven-test-interop-print")
    set_default(false)
    add_rules("@carven/carven", {tests = "default"})
    set_values("carven.includedir", crafts_dir)
    set_languages("c++23")
    add_files(path.join(interop_dir, "output", "print.cv"))
    after_load(use_local_carven)
    add_tests("output", {group = "interop"})
    on_test(function (target)
        local output, errors = os.iorunv(target:targetfile(), {}, {timeout = 30000})
        assert(output:gsub("\r\n", "\n") == "Literal\nValue: {123}\n" and errors == "",
            "C++23 interop produced unexpected output:\n%s\n%s", output, errors)
        return true
    end)

local rejection_cases = {
    ["pointers/const_conversion"] = {"cannot initialize", "pointer_probe::readonly_fixed", site = "source"},
    ["pointers/noncopyable_target"] = {"deleted constructor", "Fixed", site = "source"},
    ["pointers/native_double_output"] = {"cannot initialize a parameter", "pointer_probe::output", site = "source"},
    ["pointers/self_dependent_callable"] = {
        "incomplete type", "Node", site = "header",
        note = "'ReadArg'", line_contains = "ReadArg<Node>",
    },
    ["interpolation/invalid_specification"] = {"format", "format", site = "source"},
    ["interpolation/wrong_type"] = {"format", "format", site = "source"},
    ["interpolation/unicode_char_is_text"] = {"format", "format", site = "source"},
    ["interpolation/missing_formatter"] = {"format", "formatter", site = "source"},
}

target("carven-test-interop-rejections")
    set_default(false)
    set_kind("phony")
    set_languages("c++20")
    add_deps("carven", {inherit = false})
    for _, name in ipairs(table.orderkeys(rejection_cases)) do
        add_tests(name, {group = "interop"})
    end
    on_test(function (target, opt)
        local name = opt.name:match("^[^/]+/(.+)$") or opt.name
        import("harness.compile", {rootdir = interop_dir})(target, name, rejection_cases[name])
        return true
    end)
