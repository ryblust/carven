rule("carven")
    set_extensions(".cv")
    add_deps("utils.compiler.runtime")
    add_deps("utils.inherit.links")
    add_deps("utils.merge.object", "utils.merge.archive")
    add_deps("utils.symbols.extract")

    on_config(function (target)
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
        import("lib.detect.find_tool")

        local function find_carven_program(target)
            local program = target:values("carven.program")
            if program and #program > 0 then
                return program
            end

            local package = target:pkg("carven")
            if package then
                local tool = package:find_tool("carven", {check = "--version", force = true})
                if tool then
                    return tool.program
                end
            end

            return assert(find_tool("carven", {check = "--version", force = true}), "carven not found!").program
        end

        local carven_program = find_carven_program(target)
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
        batchcmds:add_depvalues(carven_program, standard or "", target:values("carven.import_std") and "import_std" or "no_import_std")
        batchcmds:set_depmtime(os.mtime(objectfile))
        batchcmds:set_depcache(target:dependfile(objectfile))
    end)
