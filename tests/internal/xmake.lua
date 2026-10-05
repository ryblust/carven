target("carven-test-internal")
    set_default(false)
    add_deps("carven-modules", "carven-test-support")

    add_includedirs(path.join(os.projectdir(), "crafts"))
    add_files(path.join(os.projectdir(), "tests", "harness", "main.cpp"))
    add_files(path.join(os.projectdir(), "tests", "internal", "**.cppm"))
    add_files(path.join(os.projectdir(), "tests", "internal", "**.cpp"))

    add_tests("internal", {group = "internal", run_timeout = 60000,
        runargs = {"--exclude", "Static specialization budgets:*"}})
    -- Each production-budget contract gets its own result and timeout. Combining
    -- them makes sanitizer overhead accumulate and hides which case is slow.
    for _, budget in ipairs({
        {name = "iteration", test = "default iteration boundary"},
        {name = "instance", test = "default instance boundary is independent per root"},
        {name = "nesting", test = "default nesting boundary"},
        {name = "node", test = "default node limit counts copied operations"},
    }) do
        add_tests("specialization-" .. budget.name .. "-budget", {
            group = "internal", run_timeout = 60000,
            runargs = {"--test", "Static specialization budgets: " .. budget.test},
        })
    end
target_end()
