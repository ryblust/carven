local output = [[
Read 8 records; unique: 4; duplicates: 4
alpha
beta
gamma
delta
]]

target("carven-example-stream-collector")
    set_kind("binary")
    add_rules("carven")
    add_includedirs(".")
    add_files("main.cpp", "workflow.cv", "stream.cpp")
    add_tests("behavior", {
        files = {"checks.cpp"}, remove_files = {"main.cpp"}, run_timeout = 30000,
    })
    add_tests("output", {
        run_timeout = 30000, plain = true,
        pass_outputs = {output, (output:gsub("\n", "\r\n"))},
    })
target_end()
