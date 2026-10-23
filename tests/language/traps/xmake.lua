local reports = {
    ["remainder"] = {"operations.cv:", "division by zero"},
    ["shift"] = {"error: shift count is outside the integer type width"},
    ["width"] = {"error: shift count is outside the integer type width"},
    ["slice-index"] = {"sequence index is out of bounds"},
    ["slice-negative"] = {"sequence index is out of bounds"},
    ["slice-range"] = {"slice range is out of bounds"},
    ["slice-reversed"] = {"slice range is out of bounds"},
    ["slice-empty"] = {"slice range is out of bounds"},
    ["slice-format"] = {"slice range is out of bounds"},
    ["unicode"] = {"value is not a Unicode scalar value"},
    ["unicode-export"] = {"value is not a Unicode scalar value"},
    ["formatted-text"] = {"text is not valid UTF-8"},
    ["clamp"] = {"assertion failed", "lower <= upper"},
    ["simd-clamp"] = {"assertion failed", "lower <= upper"},
    ["simd-load"] = {"SIMD index or memory range is out of bounds"},
    ["simd-partial"] = {"SIMD index or memory range is out of bounds"},
    ["simd-lane"] = {"SIMD index or memory range is out of bounds"},
    ["simd-with-lane"] = {"SIMD index or memory range is out of bounds"},
    ["simd-prefix"] = {"SIMD index or memory range is out of bounds"},
    ["outside-test"] = {"operations.cv:", "test operation requires an active test"},
    ["assertion-snapshot"] = {
        "message evaluated", "actual == expected", "actual: [\n        1,\n        11,\n    ]",
        "expected: [\n        2,\n        22,\n    ]", "array mismatch",
    },
    ["fatal-tests"] = {
        "fatal.cv:", "name: fatal assertion", "earlier failure retained", "earlier check retained", "stop the run",
    },
    ["divide"] = {"operations.cv:", "division by zero"},
    ["index"] = {"operations.cv:", "sequence index is out of bounds", "index: 1", "length: 1"},
    ["slice-length"] = {"operations.cv:", "slice range is out of bounds"},
    ["assertion"] = {"operations.cv:", "assertion failed", "native assertion"},
}

target("carven-test-language-traps")
    set_kind("binary")
    add_rules("carven", {tests = {entry = false}})
    add_defines("NDEBUG")
    add_files("operations.cv", "fatal.cv", "runner.cpp")
    add_includedirs(".")
    for _, name in ipairs(table.orderkeys(reports)) do
        add_tests(name, {run_timeout = 30000, runargs = {name}, plain = true, pass_outputs = {""}})
    end
    after_test(function (target, opt)
        local name = opt.name:match("([^/]+)$")
        local stderr = (opt.stderr or ""):gsub("\r\n", "\n")
        assert(stderr:endswith("  note: execution aborted\n\n"), stderr)
        for _, fragment in ipairs(reports[name]) do
            assert(stderr:find(fragment, 1, true), "missing trap report: " .. fragment)
        end
        assert(not stderr:find("unreachable", 1, true), stderr)
        if name == "assertion-snapshot" then
            assert(not stderr:find("99,", 1, true), stderr)
        elseif name == "fatal-tests" then
            assert(not stderr:find("carven: tests:", 1, true), stderr)
        end
    end)
target_end()
