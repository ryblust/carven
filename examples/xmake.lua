local examples_dir = path.join(os.projectdir(), "examples")

local cases = {
    {name = "hello", source = "hello.cv", output = "Hello, Carven!\n"},
    {name = "log-analyzer", dir = "log_analyzer", output = [[
{"accepted":3,"rejected":1,"warnings":1,"errors":1,"slow":["mixer token=[redacted] clipped=2","upstream timed out"]}
]]},
    {name = "audio-mixer", dir = "audio_mixer", output = [[
Mixed 10 samples; clipped: 2
Peak: 1.00; RMS: 0.652
[
    0.5,
    -0.5,
    1,
    -1,
    0.25,
    0.5,
    -0.5,
    0.25,
    0.75,
    -0.75,
]
]]},
    {name = "stream-collector", dir = "stream_collector", native = true, output = [[
Read 8 records; unique: 4; duplicates: 4
alpha
beta
gamma
delta
]]},
}

local names = {}
for _, case in ipairs(cases) do
    local name = "carven-example-" .. case.name
    table.insert(names, name)
    target(name)
        set_default(false)
        set_kind("binary")
        set_languages("c++20")
        add_rules("@carven/carven")
        if case.source then
            add_files(path.join(examples_dir, case.source))
        else
            local directory = path.join(examples_dir, case.dir)
            add_files(path.join(directory, "*.cv"))
            if case.native then
                add_includedirs(directory)
                add_files(path.join(directory, "*.cpp"))
            end
        end
        add_tests("output", {
            group = "examples",
            run_timeout = 30000,
            plain = true,
            pass_outputs = {case.output, (case.output:gsub("\n", "\r\n"))},
        })
    target_end()
end

target("carven-test-examples-behavior")
    set_default(false)
    set_languages("c++20")
    add_rules("@carven/carven", {tests = "main"})
    add_files(path.join(examples_dir, "log_analyzer", "analysis.cv"), path.join(examples_dir, "audio_mixer", "mix.cv"))
    add_tests("behavior", {group = "examples", run_timeout = 30000})
target_end()
table.insert(names, "carven-test-examples-behavior")

target("carven-test-stream-collector")
    set_default(false)
    set_kind("binary")
    set_languages("c++20")
    add_rules("@carven/carven")
    add_includedirs(path.join(examples_dir, "stream_collector"))
    add_files(path.join(examples_dir, "stream_collector", "workflow.cv"))
    add_files(path.join(examples_dir, "stream_collector", "stream.cpp"))
    add_files(path.join(os.projectdir(), "tests", "examples", "stream_collector.cpp"))
    add_tests("behavior", {group = "examples", run_timeout = 30000})
target_end()
table.insert(names, "carven-test-stream-collector")

target("carven-test-examples")
    set_default(false)
    set_kind("phony")
    add_deps("carven-example-log-analyzer", "carven-example-audio-mixer", "carven-example-stream-collector")
    for _, name in ipairs({
        "pipeline", "configuration", "arguments", "record-recovery",
    }) do
        add_tests(name, {group = "examples", run_timeout = 30000})
    end
    on_test(function (target, opt)
        import("checks", {rootdir = examples_dir}).main(opt.name:match("([^/]+)$"))
        return true
    end)
target_end()
table.insert(names, "carven-test-examples")

target("examples")
    set_default(false)
    set_kind("phony")
    add_deps(table.unpack(names))
