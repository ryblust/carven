local interop_dir = path.join(os.projectdir(), "tests", "interop")

local interop_sources = {}
for _, domain in ipairs({
    "bindings",
    "discarded_results",
    "interpolation",
    "lifetimes",
    "pointers",
    "providers",
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
table.insert(interop_sources, path.join(interop_dir, "providers", "generated", "generated_record", "Result.cv"))

local rejection_cases = {
    ["pointers/network_owner"] = {"deleted constructor", "Connection"},
    ["construction/folded_narrowing"] = {"cannot be narrowed", "unsigned char"},
    ["construction/read_narrowing"] = {"cannot be narrowed", "unsigned char"},
    ["contracts/result"] = {"no viable conversion", "contract_text"},
    ["contracts/access"] = {"drops 'const' qualifier", "contract_replace"},
    ["pointers/const_conversion"] = {"cannot initialize", "pointer_probe::readonly_fixed"},
    ["pointers/noncopyable_target"] = {"deleted constructor", "Fixed"},
    ["pointers/double_output"] = {"cannot initialize a parameter", "pointer_probe::output"},
    ["interpolation/invalid_specification"] = {"format", "format"},
    ["interpolation/missing_formatter"] = {
        "format", "formatter", note = "std::basic_format_string",
    },
}

target("carven-test-interop")
    set_default(false)
    add_rules("@carven/carven", {tests = "runner"})

    set_languages("c++20")
    add_includedirs(interop_dir)
    for _, source in ipairs(interop_sources) do
        add_files(source)
    end

    add_tests("behavior", {group = "interop"})
    add_tests("structural-output", {group = "interop"})
    for _, operation in ipairs({
        "divide", "remainder", "shift", "width", "index",
        "slice-index", "slice-negative", "slice-range", "slice-reversed",
        "slice-known-length", "slice-known-empty", "slice-known-format",
        "unicode", "unicode-export", "contract-stop", "math-reversed"
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
    {name = "carven-test-interop-simd-portable", portable = true},
}) do
    target(variant.name)
        set_default(false)
        add_rules("@carven/carven", {tests = "runner"})
        set_languages("c++20")
        if variant.portable then
            add_defines("CARVEN_SIMD_FORCE_PORTABLE")
        else
            add_vectorexts("avx2")
        end
        add_includedirs(interop_dir)
        add_files(path.join(interop_dir, "simd", "**.cv"))
        add_files(path.join(interop_dir, "simd", "runner.cpp"))
        add_files(path.join(interop_dir, "simd", "utf.cpp"))
        add_tests("behavior", {group = "interop", run_timeout = 30000})
        if not variant.portable then
            for _, operation in ipairs({"load", "load-partial", "lane", "with-lane", "prefix", "clamp-unordered"}) do
                add_tests(operation, {group = "interop", run_timeout = 30000})
            end
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

target("carven-test-interop-compilation")
    set_default(false)
    set_kind("phony")
    set_languages("c++20")
    add_deps("carven", {inherit = false})
    for _, name in ipairs(table.orderkeys(rejection_cases)) do
        add_tests(name, {group = "interop"})
    end
    add_tests("library-consumer", {group = "interop", run_timeout = 30000})
    add_tests("toolchain-consumer", {group = "interop", run_timeout = 300000})
    on_test(function (target, opt)
        local name = opt.name:match("^[^/]+/(.+)$") or opt.name
        if name == "library-consumer" then
            import("harness.library", {rootdir = interop_dir}).main(target)
        elseif name == "toolchain-consumer" then
            import("harness.toolchain", {rootdir = interop_dir}).main()
        else
            import("harness.compile", {rootdir = interop_dir})(target, name, rejection_cases[name])
        end
        return true
    end)
target_end()
for _, mode in ipairs({
    {standard = "c++20", suffix = "", operations = {
        "copy", "move", "function", "format-throw", "format-utf8",
        "append-format-throw", "print-inner-throw", "print-later-throw",
        "async-body", "async-awaiter", "async-success",
        "import-success", "import-throw",
    }},
    {standard = "c++23", suffix = "-cxx23", operations = {"print-later-throw"}},
}) do
    target("carven-test-interop-exception-boundary" .. mode.suffix)
        set_default(false)
        set_kind("binary")
        add_rules("@carven/carven")
        set_languages(mode.standard)
        set_exceptions("cxx")
        add_includedirs(interop_dir)
        add_files(path.join(interop_dir, "exceptions", "terminate.cpp"))
        add_files(path.join(interop_dir, "exceptions", "precomputed.cv"))
        for _, operation in ipairs(mode.operations) do
            add_tests(operation, {group = "interop"})
        end
        on_test(function (target, opt)
            local operation = opt.name:match("([^/]+)$")
            local prefixes = {["print-inner-throw"] = "", ["print-later-throw"] = "42 "}
            import("harness.process", {rootdir = interop_dir})(
                target, {operation}, operation == "import-success" and 0 or 73, operation, prefixes[operation])
            return true
        end)
    target_end()
end

target("carven-test-interop-print")
    set_default(false)
    add_rules("@carven/carven", {tests = "main"})

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

local async_outputs = {
    net = "network: pass\n",
    timer = "cold: pass\nimmediate: pass\nfuture: pass\nsiblings: pass\n"
        .. "pre-cancel: pass\ncancelled-close: pass\ncommitted: pass\nrearm: pass\nmulti-source: pass\n",
    cleanup = "async lexical cleanup passed\n",
}
if is_plat("macosx", "linux") then
    async_outputs.pipe = "cold: pass\ntransfer: pass\neof-error: pass\ndelayed: pass\nidle-wait: pass\n"
        .. "timer-deadline: pass\ntimer-cancel: pass\n"
        .. "cancelled-close: pass\ncommitted: pass\nrearm: pass\nbackpressure: pass\n"
    async_outputs.bridge = "cold: pass\nready: pass\npending: pass\nfailures: pass\n"
        .. "cancelled-close: pass\ntrivial: pass\ncharacters: pass\n"
        .. "chunks: pass\npending-read: pass\nbackpressure: pass\nfailure: pass\ncancelled-close: pass\n"
end

target("carven-test-interop-async")
    set_default(false)
    set_kind("binary")
    add_rules("@carven/carven")
    set_languages("c++20")
    add_includedirs(interop_dir)
    add_files(path.join(interop_dir, "async", "runner.cpp"))
    add_files(path.join(interop_dir, "async", "net.cv"), path.join(interop_dir, "async", "net.cpp"))
    add_files(path.join(interop_dir, "async", "timer.cpp"))
    add_files(path.join(interop_dir, "async", "ownership.cv"))
    add_files(path.join(interop_dir, "async", "cleanup.cv"))
    add_files(path.join(interop_dir, "async", "cleanup.cpp"))
    for _, case in ipairs({"fixed", "tracked", "tail"}) do
        add_files(path.join(interop_dir, "async", case .. ".cv"))
        add_files(path.join(interop_dir, "async", case .. ".cpp"))
    end
    if is_plat("macosx", "linux") then
        for _, case in ipairs({"bridge", "transfer"}) do
            add_files(path.join(interop_dir, "async", case .. ".cv"))
            add_files(path.join(interop_dir, "async", case .. ".cpp"))
            add_files(path.join(interop_dir, "async", case .. "_provider.cpp"))
        end
        add_files(path.join(interop_dir, "async", "pipe.cpp"))
        for _, operation in ipairs({
            "invalidchar", "invalidchar-discard", "invalidchar-join", "invalidchar-race",
            "invalidchar-first", "invalidchar-join-later-failure",
        }) do
            add_tests(operation, {group = "interop", run_timeout = 30000})
        end
    end
    if is_plat("linux") then
        add_syslinks("pthread")
    end
    add_tests("ownership", {group = "interop", run_timeout = 30000})
    for _, name in ipairs(table.orderkeys(async_outputs)) do
        add_tests(name, {group = "interop", run_timeout = 30000})
    end
    on_test(function (target, opt)
        local name = opt.name:match("([^/]+)$")
        if name:find("^invalidchar") then
            local stdout_file, stderr_file = os.tmpfile(), os.tmpfile()
            local code = os.execv(target:targetfile(), {name}, {
                try = true, timeout = 30000, stdout = stdout_file, stderr = stderr_file,
            })
            local output = os.isfile(stdout_file) and io.readfile(stdout_file) or ""
            local errors = os.isfile(stderr_file) and io.readfile(stderr_file) or ""
            os.tryrm(stdout_file)
            os.tryrm(stderr_file)
            assert(code == 73 and output == ""
                and errors:find("value is not a Unicode scalar value", 1, true)
                and (not name:find("invalidchar%-join") and name ~= "invalidchar-race"
                    and name ~= "invalidchar-first" or errors:find("bridge.cv:", 1, true)),
                "async " .. name .. " must report invalid success: " .. tostring(code) .. output .. errors)
            return true
        end
        local output, errors = os.iorunv(target:targetfile(), {name}, {timeout = 30000})
        local expected = name == "ownership"
            and io.readfile(path.join(interop_dir, "async", "ownership.expected"))
            or async_outputs[name]
        assert(output:gsub("\r\n", "\n") == expected,
            "async " .. name .. " behavior differs: " .. output)
        assert(errors == "", "unexpected async " .. name .. " report: " .. errors)
        return true
    end)
target_end()
