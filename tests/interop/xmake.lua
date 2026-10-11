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
    "number",
    "scalars",
    "text",
    "time",
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
    for _, source in ipairs(interop_sources) do
        add_files(source, {includedirs = interop_dir})
    end

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
        add_files(path.join(interop_dir, "simd", "**.cv"), {includedirs = interop_dir})
        add_files(path.join(interop_dir, "simd", "runner.cpp"), {includedirs = interop_dir})
        add_files(path.join(interop_dir, "simd", "utf.cpp"), {includedirs = interop_dir})
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
for _, mode in ipairs({
    {standard = "c++20", suffix = "", operations = {
        "copy", "move", "function", "format-throw", "format-utf8",
        "append-format-throw", "print-inner-throw", "print-later-throw",
        "async-body", "async-awaiter", "async-success",
    }},
    {standard = "c++23", suffix = "-cxx23", operations = {"print-later-throw"}},
}) do
    target("carven-test-interop-exception-boundary" .. mode.suffix)
        set_default(false)
        set_kind("binary")
        add_rules("@carven/carven")
        set_languages(mode.standard)
        set_exceptions("cxx")
        add_files(path.join(interop_dir, "exceptions", "terminate.cpp"), {
            includedirs = interop_dir,
        })
        add_files(path.join(interop_dir, "exceptions", "precomputed.cv"), {
            includedirs = interop_dir,
        })
        for _, operation in ipairs(mode.operations) do
            add_tests(operation, {group = "interop"})
        end
        on_test(function (target, opt)
            local operation = opt.name:match("([^/]+)$")
            local prefixes = {["print-inner-throw"] = "", ["print-later-throw"] = "42 "}
            import("harness.process", {rootdir = interop_dir})(
                target, {operation}, 73, operation, prefixes[operation])
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

-- One program observes native result ownership across the source async boundary.
target("carven-test-interop-async-ownership")
    set_default(false)
    add_rules("@carven/carven")
    set_languages("c++20")
    add_files(path.join(interop_dir, "async", "ownership.cv"), {includedirs = interop_dir})
    for _, case in ipairs({"fixed", "tracked", "tail"}) do
        add_files(path.join(interop_dir, "async", case .. ".cv"), {includedirs = interop_dir})
        add_files(path.join(interop_dir, "async", case .. ".cpp"), {includedirs = interop_dir})
    end
    add_tests("ownership", {group = "interop", run_timeout = 30000})
    on_test(function (target)
        local output, errors = os.iorunv(target:targetfile(), {}, {timeout = 30000})
        local expected = io.readfile(path.join(interop_dir, "async", "ownership.expected"))
        assert(output:gsub("\r\n", "\n") == expected, "async ownership differs: " .. output)
        assert(errors == "", "unexpected async ownership report: " .. errors)
        return true
    end)
target_end()


target("carven-test-interop-async-timer")
    set_default(false)
    set_kind("binary")
    set_languages("c++20")
    add_includedirs(crafts_dir, interop_dir)
    add_files(path.join(interop_dir, "async", "timer.cpp"))
    add_tests("provider", {group = "interop", run_timeout = 30000})
    on_test(function (target)
        local output, errors = os.iorunv(target:targetfile(), {}, {timeout = 30000})
        local expected = "cold: pass\nimmediate: pass\nfuture: pass\nsiblings: pass\n"
            .. "pre-cancel: pass\ncancelled-close: pass\ncommitted: pass\nrearm: pass\n"
        assert(output:gsub("\r\n", "\n") == expected, "async timer contracts differ: " .. output)
        assert(errors == "", "unexpected async timer report: " .. errors)
        return true
    end)
target_end()


if is_plat("macosx", "linux") then
    target("carven-test-interop-async-pipe")
        set_default(false)
        set_kind("binary")
        set_languages("c++20")
        add_includedirs(crafts_dir, interop_dir)
        add_files(path.join(interop_dir, "async", "pipe.cpp"))
        if is_plat("linux") then
            add_syslinks("pthread")
        end
        add_tests("provider", {group = "interop", run_timeout = 30000})
        on_test(function (target)
            local output, errors = os.iorunv(target:targetfile(), {}, {timeout = 30000})
            local expected = "cold: pass\ntransfer: pass\neof-error: pass\ndelayed: pass\nidle-wait: pass\n"
                .. "timer-deadline: pass\ntimer-cancel: pass\n"
                .. "cancelled-close: pass\ncommitted: pass\nrearm: pass\nbackpressure: pass\n"
            assert(output:gsub("\r\n", "\n") == expected, "async pipe contracts differ: " .. output)
            assert(errors == "", "unexpected async pipe report: " .. errors)
            return true
        end)
    target_end()
end


if is_plat("macosx", "linux") then
    target("carven-test-interop-async-bridge")
        set_default(false)
        set_kind("binary")
        add_rules("@carven/carven")
        set_languages("c++20")
        add_files(path.join(interop_dir, "async", "bridge.cv"), {includedirs = interop_dir})
        add_files(path.join(interop_dir, "async", "bridge.cpp"), {includedirs = interop_dir})
        add_files(path.join(interop_dir, "async", "bridge_provider.cpp"), {includedirs = interop_dir})
        add_files(path.join(interop_dir, "async", "transfer.cv"), {includedirs = interop_dir})
        add_files(path.join(interop_dir, "async", "transfer.cpp"), {includedirs = interop_dir})
        add_files(path.join(interop_dir, "async", "transfer_provider.cpp"), {includedirs = interop_dir})
        if is_plat("linux") then
            add_syslinks("pthread")
        end
        add_tests("provider", {group = "interop", run_timeout = 30000})
        add_tests("invalidchar", {group = "interop", run_timeout = 30000})
        add_tests("invalidchar-discard", {group = "interop", run_timeout = 30000})
        add_tests("invalidchar-join", {group = "interop", run_timeout = 30000})
        add_tests("invalidchar-race", {group = "interop", run_timeout = 30000})
        add_tests("invalidchar-first", {group = "interop", run_timeout = 30000})
        add_tests("invalidchar-join-later-failure", {group = "interop", run_timeout = 30000})
        on_test(function (target, opt)
            local name = opt.name:match("([^/]+)$")
            if name ~= "provider" then
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
            local output, errors = os.iorunv(target:targetfile(), {}, {timeout = 30000})
            local expected = "cold: pass\nready: pass\npending: pass\nfailures: pass\n"
                .. "cancelled-close: pass\ntrivial: pass\ncharacters: pass\n"
                .. "chunks: pass\npending-read: pass\nbackpressure: pass\nfailure: pass\ncancelled-close: pass\n"
            assert(output:gsub("\r\n", "\n") == expected, "async source/native bridge contracts differ: " .. output)
            assert(errors == "", "unexpected async source/native bridge report: " .. errors)
            return true
        end)
    target_end()
end


target("carven-test-interop-async-cleanup")
    set_default(false)
    set_kind("binary")
    add_rules("@carven/carven")
    set_languages("c++20")
    add_files(path.join(interop_dir, "async", "cleanup.cv"), {includedirs = interop_dir})
    add_files(path.join(interop_dir, "async", "cleanup.cpp"), {includedirs = interop_dir})
    add_tests("lexical-cleanup", {group = "interop", run_timeout = 30000})
    on_test(function (target)
        local output, errors = os.iorunv(target:targetfile(), {}, {timeout = 30000})
        assert(output:gsub("\r\n", "\n") == "async lexical cleanup passed\n" and errors == "",
            "async lexical cleanup contract differs: " .. output .. errors)
        return true
    end)
target_end()
