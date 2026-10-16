local interop_dir = path.join(os.projectdir(), "tests", "interop")

target("carven-test-interop")
    set_default(false)
    set_group("interop")
    set_kind("binary")
    add_rules("@carven/carven", {tests = true})

    set_languages("c++20")
    add_includedirs(interop_dir)
    for _, domain in ipairs({
        "bindings", "discarded_results", "interpolation", "lifetimes", "pointers", "providers", "scalars", "text",
    }) do
        for _, extension in ipairs({"cv", "cpp"}) do
            local pattern = path.join(interop_dir, domain, "*." .. extension)
            if #os.files(pattern) > 0 then
                add_files(pattern)
            end
        end
    end
    add_files(path.join(interop_dir, "printing", "structural.cv"))
    add_files(path.join(interop_dir, "providers", "generated", "generated_record", "Result.cv"))

    local output = "<opaque>\ncustom\nEnvelope {\n    value: <opaque>,\n}\n1\ntrue 1.5 65\n<opaque>\nnullptr\n"
        .. 'native\ntext mutable\n[\n    "native\\ntext",\n    nullptr,\n]\n'
        .. string.rep("x", 17000) .. "\n"
    add_tests("default", {
        plain = true,
        run_timeout = 30000,
        pass_outputs = {output, (output:gsub("\n", "\r\n"))},
    })
target_end()

for _, variant in ipairs({
    {name = "carven-test-interop-simd"},
    {name = "carven-test-interop-simd-portable", portable = true},
}) do
    target(variant.name)
        set_default(false)
        set_group("interop")
        set_kind("binary")
        add_rules("@carven/carven", {tests = true})
        set_languages("c++20")
        if variant.portable then
            add_defines("CARVEN_SIMD_FORCE_PORTABLE")
        else
            add_vectorexts("avx2")
        end
        add_includedirs(interop_dir)
        add_files(path.join(interop_dir, "simd", "**.cv"))
        add_files(path.join(interop_dir, "simd", "storage.cpp"))
        add_files(path.join(interop_dir, "simd", "utf.cpp"))
        add_tests("default", {run_timeout = 30000})
    target_end()
end

target("carven-test-interop-print")
    set_default(false)
    set_kind("binary")
    set_group("interop")
    add_rules("@carven/carven", {tests = true})
    set_languages("c++23")
    add_files(path.join(interop_dir, "printing", "print.cv"))
    add_tests("default", {
        plain = true,
        run_timeout = 30000,
        pass_outputs = {"Literal\nValue: {123}\n", "Literal\r\nValue: {123}\r\n"},
    })
target_end()

target("carven-test-interop-native")
    set_default(false)
    set_group("interop")
    set_kind("binary")
    add_rules("@carven/carven", {tests = {entry = false}})
    set_languages("c++20")
    add_includedirs(interop_dir, path.join(interop_dir, "stops"))
    add_files(path.join(interop_dir, "execution", "runner.cpp"))
    add_files(path.join(interop_dir, "stops", "*.cv"), path.join(interop_dir, "stops", "*.cpp"))
    add_tests("native-stop", {
        run_timeout = 30000, runargs = {"native-stop"}, plain = true, pass_outputs = {""},
    })
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
        for _, name in ipairs({"invalidchar", "invalidchar-discard", "invalidchar-join",
            "invalidchar-race", "invalidchar-first", "invalidchar-join-later-failure"}) do
            add_tests(name, {run_timeout = 30000, runargs = {name}, plain = true, pass_outputs = {""}})
        end
    end
    if is_plat("linux") then
        add_syslinks("pthread")
    end
    local cases = {"ownership", "net", "timer", "cleanup"}
    if is_plat("macosx", "linux") then
        table.insert(cases, "pipe")
        table.insert(cases, "bridge")
    end
    for _, name in ipairs(cases) do
        add_tests(name, {run_timeout = 30000, runargs = {name}})
    end
    after_test(function (target, opt)
        local name = opt.name:match("([^/]+)$")
        local stderr = (opt.stderr or ""):gsub("\r\n", "\n")
        local reports
        if name == "native-stop" then
            reports = {"error: requirement failed", "name: test stop at a native boundary",
                "native boundary stop", "note: test stopped"}
            assert(stderr:match("callback%.cv:%d+:%d+: error:"), stderr)
            assert(not stderr:find("unreachable", 1, true), stderr)
            assert(not stderr:find("carven: tests:", 1, true), stderr)
        elseif name:startswith("invalidchar") then
            reports = {"error: value is not a Unicode scalar value"}
            assert(stderr:match("bridge%.cv:%d+:%d+: error:"), stderr)
            assert(stderr:endswith("  note: execution aborted\n\n"), stderr)
        else
            return
        end
        for _, fragment in ipairs(reports) do
            assert(stderr:find(fragment, 1, true), "missing interop report: " .. fragment)
        end
    end)
target_end()

includes("linkage")
