set_default(false)
set_group("examples")
set_languages("c++20")

target("carven-example-hello")
    set_kind("binary")
    add_rules("carven")
    add_files("hello.cv")
    add_tests("output", {
        run_timeout = 30000, plain = true,
        pass_outputs = {"Hello, Carven!\n", "Hello, Carven!\r\n"},
    })
target_end()

includes("audio_mixer", "log_analyzer", "stream_collector", "runtime")

target("examples")
    set_kind("phony")
    add_deps("carven-example-hello", "carven-example-audio-mixer",
        "carven-example-log-analyzer", "carven-example-stream-collector", "carven-example-runtime")
target_end()

target("carven-example-pipeline")
    set_kind("phony")
    add_deps("carven-example-audio-mixer", "carven-example-log-analyzer")
    add_tests("pipeline", {run_timeout = 30000})
    on_test(function (target)
        import("checks", {rootdir = path.join(os.projectdir(), "examples")})
            .pipeline(target)
        return true
    end)
target_end()
