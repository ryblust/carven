rule("carven.build.cv")
    set_extensions(".cv")

    on_load(function (target)
        local sourcekinds = target:sourcekinds()
        if #sourcekinds == 0 then
            table.insert(sourcekinds, "cxx")
        end

        local carven = target:pkg("carven")
        if carven and carven:installdir() then
            local includedir = path.join(carven:installdir(), "include")
            if os.isdir(includedir) then
                target:add("includedirs", includedir)
            end
        end
    end)

    on_buildcmd_file(function (target, batchcmds, sourcefile_cv, opt)
        local carven_program = target:values("carven.program")
        if not carven_program then
            carven_program = os.getenv("CARVEN")
        end
        if not carven_program or #carven_program == 0 then
            local carven = target:pkg("carven")
            if carven then
                local tool = carven:find_tool("carven", {check = "--version"})
                if tool then
                    carven_program = tool.program
                end
            end
        end
        if not carven_program or #carven_program == 0 then
            import("lib.detect.find_tool")
            local carven = assert(find_tool("carven", {check = "--version"}), "carven not found!")
            carven_program = carven.program
        end

        local sourcefile_cpp = target:autogenfile((sourcefile_cv:gsub("%.cv$", ".cpp")))
        local basedir = path.directory(sourcefile_cpp)
        local objectfile = target:objectfile(sourcefile_cpp)
        table.insert(target:objectfiles(), objectfile)

        local argv = {"transpile"}
        local standard = target:values("carven.standard")
        if standard then
            table.insert(argv, "-std=" .. standard)
        end
        if target:values("carven.import_std") then
            table.insert(argv, "--import-std")
        end
        table.insert(argv, "-o")
        table.insert(argv, path(sourcefile_cpp))
        table.insert(argv, path(sourcefile_cv))

        batchcmds:show_progress(opt.progress, "${color.build.object}transpiling.cv %s", sourcefile_cv)
        batchcmds:mkdir(basedir)
        batchcmds:vrunv(carven_program, argv)
        batchcmds:compile(sourcefile_cpp, objectfile)

        batchcmds:add_depfiles(sourcefile_cv)
        batchcmds:set_depmtime(os.mtime(objectfile))
        batchcmds:set_depcache(target:dependfile(objectfile))
    end)

rule("carven")
    add_deps("carven.build.cv")
    add_deps("utils.compiler.runtime")
    add_deps("utils.inherit.links")
    add_deps("utils.merge.object", "utils.merge.archive")
    add_deps("utils.symbols.extract")
