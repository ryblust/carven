set_xmakever("3.1.1")

local function rule_option(target, name)
    return target:extraconf("rules", "carven", name)
end

local directory = os.scriptdir()

local function has_cpp_language(target)
    for _, language in ipairs(table.wrap(target:get("languages"))) do
        if language:match("^c%+%+") or language:match("^cxx") or
            language:match("^gnu%+%+") or language:match("^gnuxx")
        then
            return true
        end
    end
    return false
end

rule("carven.build")
    set_extensions(".cv")

    before_config(function (target)
        import("generation", {rootdir = directory}).reset(target)
    end)

    after_config(function (target, opt)
        local generation = import("generation", {rootdir = directory})

        local sources = {}
        for _, sourcefile in ipairs(target:sourcefiles()) do
            if path.extension(sourcefile) == ".cv" then table.insert(sources, sourcefile) end
        end
        table.sort(sources)
        target:data_set("carven.sources", sources)
        if #sources == 0 then return end

        local timings = rule_option(target, "timings")
        if timings == nil then
            timings = import("core.base.option").get("verbose") or false
        elseif type(timings) ~= "boolean" then
            raise("carven: timings must be a boolean")
        end
        local tests = rule_option(target, "tests")
        if type(tests) == "table" then
            if tests.entry ~= nil and type(tests.entry) ~= "boolean" then
                raise("carven: tests.entry must be a boolean")
            end
            tests = tests.entry ~= false and true or "runner"
        elseif tests ~= nil and type(tests) ~= "boolean" then
            raise("carven: tests must be a boolean or a table with an entry option")
        end
        target:data_set("carven.tests", tests)
        if not has_cpp_language(target) then
            target:add("languages", "c++20")
        end

        local toolchain = rule_option(target, "toolchain")
        local program, craftsdir
        if toolchain then
            assert(type(toolchain) == "table" and type(toolchain.program) == "string"
                and type(toolchain.crafts) == "string", "carven: toolchain requires program and crafts paths")
            program = path.absolute(toolchain.program, os.projectdir())
            craftsdir = path.absolute(toolchain.crafts, os.projectdir())
        else
            local prefix = path.absolute("../../..", directory)
            program = path.join(prefix, "bin", os.host() == "windows" and "carven.exe" or "carven")
            craftsdir = path.join(prefix, "crafts")
            assert(os.isfile(program), "carven: missing compiler: " .. program)
        end
        assert(os.isdir(path.join(craftsdir, "carven")),
            "carven: incomplete toolchain; missing standard Crafts: " .. craftsdir)
        target:data_set("carven.program", program)
        local standard_source_root = generation.physical_path(path.join(craftsdir, "carven"))
        target:data_set("carven.timings", timings)

        local arguments, inputs = {}, {}
        for _, sourcefile in ipairs(sources) do
            local input = generation.source_input(sourcefile, standard_source_root, os.projectdir())
            inputs[sourcefile] = input
            table.insert(arguments, input.path)
        end
        table.sort(arguments)
        target:data_set("carven.source_arguments", arguments)
        local live_root = import("batches", {rootdir = directory}).attach(target, opt)

        local generated_files = {}
        for _, sourcefile_cv in ipairs(sources) do
            local source_config = target:fileconfig(sourcefile_cv) or {}
            local input = inputs[sourcefile_cv]
            -- Register generated implementations before native scanning.
            local sourcefile_cpp = path.relative(path.join(live_root, input.implementation), os.projectdir())
            local native_config = table.clone(source_config)
            -- Source-selection rules belong to .cv; native per-file
            -- settings belong to the implementation it generates.
            native_config.rule = nil
            native_config.rules = nil
            native_config.sourcekind = nil
            native_config.always_added = true
            target:extraconf_set("files", sourcefile_cpp, native_config)
            table.insert(generated_files, sourcefile_cpp)
        end
        if tests == true then
            local entry = path.relative(
                path.join(live_root, "carven", "generated", "carven-test-main.cpp"),
                os.projectdir()
            )
            target:extraconf_set("files", entry, {always_added = true})
            table.insert(generated_files, entry)
        end
        -- Publish the source set once: adding each implementation separately
        -- invalidates Xmake's complete file configuration on every input.
        target:add("files", generated_files)
        target:add("includedirs", live_root, {public = true})
        target:data_set("carven.generated_files", generated_files)
        target:data_set("carven.generated_root", live_root)
    end)

    before_prepare_files(function (target)
        import("artifacts", {rootdir = directory}).select_implementations(target)
    end)

rule("carven")
    after_config(function (target)
        import("generation", {rootdir = directory}).refresh_dependencies(target)
    end)
    add_deps("carven.build")
    add_deps("c++")
