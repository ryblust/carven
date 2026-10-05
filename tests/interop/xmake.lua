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
table.insert(interop_sources, path.join(interop_dir, "printing", "structural.cv"))

local rejection_cases = {
    ["construction/deduction"] = {"no viable constructor or deduction guide", "vector"},
    ["construction/folded_narrowing"] = {"cannot be narrowed", "unsigned char"},
    ["construction/read_narrowing"] = {"cannot be narrowed", "unsigned char"},
    ["construction/narrowing"] = {"cannot be narrowed", "unsigned char"},
    ["contracts/result"] = {"no viable conversion", "contract_text"},
    ["contracts/access"] = {"drops 'const' qualifier", "contract_replace"},
    ["pointers/const_conversion"] = {"cannot initialize", "pointer_probe::readonly_fixed"},
    ["pointers/noncopyable_target"] = {"deleted constructor", "Fixed"},
    ["pointers/double_output"] = {"cannot initialize a parameter", "pointer_probe::output"},
    ["interpolation/invalid_specification"] = {"format", "format"},
    ["interpolation/mixed_specifier"] = {"format", "format"},
    ["interpolation/append_specifier"] = {"format", "format"},
    ["interpolation/wrong_type"] = {"format", "format"},
    ["interpolation/cstring_specifier"] = {"format", "format"},
    ["interpolation/char_specifier"] = {"format", "format"},
    ["interpolation/missing_formatter"] = {
        "format", "formatter", note = "std::basic_format_string",
    },
    ["interpolation/append_formatter"] = {
        "format", "formatter", note = "std::basic_format_string",
    },
}

target("carven-test-interop")
    set_default(false)
    add_rules("@carven/carven", {tests = "external"})

    set_languages("c++20")
    add_includedirs(interop_dir)
    add_files(table.unpack(interop_sources))

    add_tests("behavior", {group = "interop"})
    add_tests("structural-output", {group = "interop"})
    for _, operation in ipairs({
        "divide", "remainder", "shift", "width", "index",
        "slice-index", "slice-negative", "slice-range", "slice-reversed",
        "slice-known-length", "slice-known-empty", "slice-known-format",
        "unicode", "unicode-export", "contract-stop"
    }) do
        add_tests(operation, {group = "interop"})
    end
    on_test(function (target, opt)
        local name = opt.name:match("([^/]+)$")
        if name == "behavior" then
            import("generated", {
                rootdir = path.join(os.projectdir(), "tests", "harness"),
            }).main(target, "interop behavior")
        elseif name == "structural-output" then
            local output, errors = os.iorunv(target:targetfile(), {name}, {timeout = 30000})
            assert(output:gsub("\r\n", "\n") == "<opaque>\ncustom\nEnvelope {\n    value: <opaque>,\n}\n1\ntrue 1.5 65\n<opaque>\nnullptr\n"
                .. 'native\ntext mutable\n[\n    "native\\ntext",\n    nullptr,\n]\n'
                .. string.rep("x", 17000) .. "\n" and errors == "",
                "structural display invoked a custom formatter or changed output: " .. output .. errors)
        else
            import("harness.process", {rootdir = interop_dir})(target, {name}, 73, name)
        end
        return true
    end)
target_end()

for _, variant in ipairs({
    {name = "carven-test-interop-simd"},
    {name = "carven-test-interop-simd-scalar", scalar = true},
}) do
    target(variant.name)
        set_default(false)
        add_rules("@carven/carven", {tests = "external"})
        set_languages("c++20")
        add_includedirs(interop_dir)
        if variant.scalar then
            add_defines("CARVEN_SIMD_FORCE_SCALAR")
        else
            add_vectorexts("avx2")
        end
        add_files(path.join(interop_dir, "simd", "**.cv"))
        add_files(path.join(interop_dir, "simd", "runner.cpp"))
        add_files(path.join(interop_dir, "simd", "utf.cpp"))
        add_tests("behavior", {group = "interop", run_timeout = 30000})
        for _, operation in ipairs({
            "wide-short-load", "wide-prefix", "short-load", "partial-offset", "lane", "mask-prefix",
            "float-short-load", "float-partial-offset", "float-lane", "float-update", "float-prefix",
        }) do
            add_tests(operation, {group = "interop", run_timeout = 30000})
        end
        on_test(function (target, opt)
            local operation = opt.name:match("([^/]+)$")
            if operation == "behavior" then
                return import("generated", {
                    rootdir = path.join(os.projectdir(), "tests", "harness"),
                }).main(target, "SIMD consumer")
            end
            import("harness.process", {rootdir = interop_dir})(target, {operation}, 73, operation)
            return true
        end)
    target_end()
end

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
target_end()
for _, fixture in ipairs({
    {
        name = "exception-boundary", source = "terminate.cpp",
        operations = {
            "copy", "move", "failure", "function", "object",
            "format-width", "format-throw", "format-utf8",
            "format-character-utf8", "format-locale-utf8",
            "append-format-throw", "append-format-utf8", "append-format-width",
            "print-inner-throw", "print-inner-utf8", "print-later-throw",
        },
    },
    {
        name = "allocation-failure", source = "allocation.cpp", allocation = true,
        operations = {
            "string-allocate", "string-copy", "format-allocate",
            "precomputed-format-allocate", "mixed-format-allocate",
            "append-format-allocate", "append-precomputed-allocate", "print-inner-allocate",
        },
    },
}) do
    local modes = {{standard = "c++20", suffix = ""}}
    if not fixture.allocation then
        table.insert(modes, {standard = "c++23", suffix = "-cxx23"})
    end
    for _, mode in ipairs(modes) do
        target("carven-test-interop-" .. fixture.name .. mode.suffix)
            set_default(false)
            set_kind("binary")
            add_rules("@carven/carven")
            set_languages(mode.standard)
            set_exceptions("cxx")
            if fixture.allocation and is_plat("windows") then
                -- DLL-internal allocations cannot use this executable's replacement operator new.
                set_runtimes("c++_static")
                -- Xmake's flag probe rejects this supported LLVM-MinGW driver option.
                add_ldflags("-static-libstdc++", {force = true})
            end
            add_includedirs(crafts_dir, interop_dir)
            add_files(path.join(interop_dir, "exceptions", fixture.source))
            add_files(path.join(interop_dir, "exceptions", "precomputed.cv"))
            for _, operation in ipairs(fixture.operations) do
                if mode.standard == "c++20" or operation == "print-later-throw" then
                    add_tests(operation, {group = "interop"})
                end
            end
            on_test(function (target, opt)
                local operation = opt.name:match("([^/]+)$")
                local prefixes = {
                    ["print-inner-allocate"] = "",
                    ["print-inner-throw"] = "",
                    ["print-inner-utf8"] = "",
                    ["print-later-throw"] = "42 ",
                }
                import("harness.process", {rootdir = interop_dir})(
                    target, {operation}, 73, operation, prefixes[operation])
                return true
            end)
        target_end()
    end
end

target("carven-test-interop-print")
    set_default(false)
    add_rules("@carven/carven", {tests = "default"})

    set_languages("c++23")
    add_files(path.join(interop_dir, "printing", "print.cv"))

    add_tests("output", {group = "interop"})
    on_test(function (target)
        local output, errors = os.iorunv(target:targetfile(), {}, {timeout = 30000})
        assert(output:gsub("\r\n", "\n") == "Literal\nValue: {123}\n"
            and errors:gsub("\r\n", "\n") == "carven: tests: 1 passed; 0 failed\n",
            "C++23 interop produced unexpected output:\n%s\n%s", output, errors)
        return true
    end)
target_end()
