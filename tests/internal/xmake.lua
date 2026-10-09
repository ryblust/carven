target("carven-test-internal")
    set_default(false)
    add_deps("carven-modules", "carven-test-support")

    add_includedirs(path.join(os.projectdir(), "crafts"))
    add_files(path.join(os.projectdir(), "tests", "harness", "main.cpp"))
    add_files(path.join(os.projectdir(), "tests", "internal", "**.cppm"))
    add_files(path.join(os.projectdir(), "tests", "internal", "**.cpp"))

    -- Leave headroom for sanitizer instrumentation and shared-runner variability.
    -- The timeout guards against stalled tests; it is not a performance threshold.
    local internal_test_timeout = has_config("sanitizers") and 180000 or 60000
    add_tests("internal", {group = "internal", run_timeout = internal_test_timeout})
target_end()
