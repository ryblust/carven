local output = [[
Read 8 records; unique: 4; duplicates: 4
alpha
beta
gamma
delta
]]

target("carven-example-stream-collector-workflow")
    set_kind("static")
    add_rules("@carven/carven")
    add_includedirs(".", {public = true})
    add_files("workflow.cv", "stream.cpp")
target_end()

target("carven-example-stream-collector")
    set_kind("binary")
    add_deps("carven-example-stream-collector-workflow")
    add_files("main.cpp")
    add_tests("behavior", {
        files = {"checks.cpp"}, remove_files = {"main.cpp"}, run_timeout = 30000,
    })
    add_tests("output", {
        run_timeout = 30000, plain = true,
        pass_outputs = {output, (output:gsub("\n", "\r\n"))},
    })
target_end()
