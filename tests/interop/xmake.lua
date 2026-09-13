local interop_dir = path.join(os.projectdir(), "tests", "interop")
local crafts_dir = path.join(os.projectdir(), "crafts")

local interop_sources = {}
for _, domain in ipairs({
    "bindings",
    "discarded_results",
    "interpolation",
    "lifetimes",
    "pointers",
    "providers",
    "runtime_headers",
    "scalars",
    "text",
}) do
    for _, extension in ipairs({"cv", "cpp"}) do
        local pattern = path.join(interop_dir, domain, "*." .. extension)
        if #os.files(pattern) > 0 then
            table.insert(interop_sources, pattern)
        end
    end
end
table.insert(interop_sources, path.join(interop_dir, "harness", "runner.cpp"))

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
    ["interpolation/missing_formatter"] = {
        "format", "formatter", site = "source", note = "std::basic_format_string",
    },
}

for _, mode in ipairs({
    {standard = "c++20", suffix = ""},
    {standard = "c++23", suffix = "-cxx23"},
}) do
    target("carven-test-interop" .. mode.suffix)
        set_default(false)
        add_rules("@carven/carven", {tests = "external"})

        set_languages(mode.standard)
        add_includedirs(interop_dir)
        add_files(table.unpack(interop_sources))

        add_tests("behavior", {group = "interop"})
        for _, operation in ipairs({"divide", "remainder", "shift", "width", "index", "slice-index", "slice-negative", "slice-range", "slice-reversed", "unicode", "unicode-export"}) do
            add_tests(operation, {group = "interop"})
        end
        on_test(function (target, opt)
            local name = opt.name:match("([^/]+)$")
            local arguments = name == "behavior" and {} or {name}
            import("harness.process", {rootdir = interop_dir})(target, arguments,
                name == "behavior" and 0 or 73, name)
            return true
        end)
    target_end()

    target("carven-test-interop-exception-boundary" .. mode.suffix)
        set_default(false)
        set_kind("binary")
        set_languages(mode.standard)
        set_exceptions("cxx")
        add_includedirs(crafts_dir)
        add_files(path.join(interop_dir, "exceptions", "terminate.cpp"))
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
    target_end()

    target("carven-test-interop-rejections" .. mode.suffix)
        set_default(false)
        set_kind("phony")
        set_languages(mode.standard)
        add_deps("carven", {inherit = false})
        for _, name in ipairs(table.orderkeys(rejection_cases)) do
            add_tests(name, {group = "interop"})
        end
        on_test(function (target, opt)
            local name = opt.name:match("^[^/]+/(.+)$") or opt.name
            import("harness.compile", {rootdir = interop_dir})(target, name, rejection_cases[name])
            return true
        end)
    target_end()
end

target("carven-test-interop-print")
    set_default(false)
    add_rules("@carven/carven", {tests = "default"})

    set_languages("c++23")
    add_files(path.join(interop_dir, "printing", "print.cv"))

    add_tests("output", {group = "interop"})
    on_test(function (target)
        local output, errors = os.iorunv(target:targetfile(), {}, {timeout = 30000})
        assert(output:gsub("\r\n", "\n") == "Literal\nValue: {123}\n" and errors == "",
            "C++23 interop produced unexpected output:\n%s\n%s", output, errors)
        return true
    end)
target_end()
