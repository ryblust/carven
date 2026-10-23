import("core.base.json")

function without_paths(values, removed)
    local paths, kept = {}, {}
    for _, value in ipairs(removed) do paths[path.absolute(value, os.projectdir())] = true end
    for _, value in ipairs(table.wrap(values)) do
        if not paths[path.absolute(value, os.projectdir())] then table.insert(kept, value) end
    end
    return kept
end

function without_values(values, removed)
    local excluded, kept = {}, {}
    for _, value in ipairs(removed) do excluded[value] = true end
    for _, value in ipairs(table.wrap(values)) do
        if not excluded[value] then table.insert(kept, value) end
    end
    return kept
end

-- Reconfigure rule-owned inputs and edges when Xmake clones a target.
function reset(target)
    local generated = table.join(target:data("carven.generated_files") or {},
        target:data("carven.dependency_files") or {})
    if #generated > 0 then target:set("files", without_paths(target:get("files"), generated)) end
    local roots = table.wrap(target:data("carven.generated_root"))
    if #roots > 0 then target:set("includedirs", without_paths(target:get("includedirs"), roots)) end
    local generator = target:data("carven.generation_target")
    if generator then
        local dependencies = table.copy(table.wrap(target:get("deps")))
        table.remove_if(dependencies, function (_, name) return name == generator end)
        target:set("deps", dependencies)
        target:data_set("carven.batch_target", nil)
        target:data_set("carven.generation_target", nil)
    end
    for _, name in ipairs({"includedirs", "syslinks"}) do
        local owned = target:data("carven.native_" .. name) or {}
        local without = name == "includedirs" and without_paths or without_values
        for _, value in ipairs(owned) do target:extraconf_set(name, value, {}) end
        if #owned > 0 then target:set(name, without(target:get(name), owned)) end
        target:data_set("carven.native_" .. name, nil)
    end
    target:data_set("carven.dependency_files", nil)
end

function source_label(sources)
    local label = path.relative(path.absolute(sources[1], os.projectdir()), os.projectdir())
    if #sources > 1 then label = label .. " (+" .. (#sources - 1) .. " files)" end
    return label
end

function physical_path(filename)
    local absolute = path.absolute(filename, os.projectdir())
    assert(os.exists(absolute), "carven: cannot resolve source path: " .. absolute)
    local parent = path.directory(absolute)
    if not parent or parent == absolute then return path.unix(absolute) end
    local resolved = path.join(physical_path(parent), path.filename(absolute))
    if os.islink(resolved) then
        return physical_path(path.absolute(os.readlink(resolved), path.directory(resolved)))
    end
    return path.unix(resolved)
end

function source_input(sourcefile, standard_source_root, projectdir)
    local source = path.unix(path.absolute(sourcefile, projectdir))
    local physical = physical_path(source)
    local argument = path.unix(path.relative(physical, physical_path(projectdir)))
    local prefix = physical_path(standard_source_root) .. "/"
    local logical
    if source:startswith(prefix) then
        logical = "crafts/carven/" .. source:sub(#prefix + 1)
        argument = source
    elseif physical:startswith(prefix) then
        logical = "crafts/carven/" .. physical:sub(#prefix + 1)
        argument = physical
    elseif not path.is_absolute(argument) and argument ~= ".." and not argument:startswith("../") then
        logical = argument
    else
        raise("carven: source path is outside the project and crafts: " .. tostring(sourcefile))
    end
    local module = logical:sub(1, -4):gsub("/", ".")
    return {path = argument, implementation = module:gsub("%.", "/") .. ".cpp"}
end

function input_paths(files)
    local result, seen = {}, {}
    for _, file in ipairs(files) do
        local absolute = path.absolute(file, os.projectdir())
        if not seen[absolute] then
            seen[absolute] = true
            table.insert(result, absolute)
        end
    end
    table.sort(result)
    return result
end

function read_manifest(filename)
    return assert(json.loadfile(filename))
end

function refresh_dependencies(target)
    local project = import("core.project.project")
    -- Refresh dependent targets once after configuring all generated dependencies.
    for _, consumer in pairs(project.targets()) do
        if consumer ~= target and (consumer:dep(target:name()) == target
            or consumer:dep(target:fullname()) == target) then
            consumer:set("deps", consumer:get("deps"))
        end
    end
end

function run(program, argv, timings, label, curdir)
    local stderr_file = timings and os.tmpfile() or nil
    try {
        function ()
            os.vrunv(program, argv, {curdir = curdir or os.projectdir(), stderr = stderr_file})
        end,
        finally {
            function (ok, errors)
                if stderr_file then
                    local output = io.readfile(stderr_file) or ""
                    if output ~= "" then
                        io.stderr:write("\ncarven target: " .. label .. "\n" .. output:gsub("^\r?\n", ""))
                        io.stderr:flush()
                    end
                    os.tryrm(stderr_file)
                end
                if not ok then raise(errors) end
            end
        }
    }
end

local function input_state(request, facts)
    local files = table.copy(request.inputs)
    local candidates = {}
    if facts then
        for _, artifact in ipairs(facts.artifacts) do
            table.insert(files, path.join(request.root, artifact.path))
        end
        if request.analysis then
            for _, input in ipairs(facts.inputs) do
                table.insert(files, path.absolute(input.path, facts.workspace.root))
            end
            for _, root in ipairs(input_paths(facts.library_roots)) do
                table.insert(candidates, root)
                local sources = os.files(path.join(root, "**.cv"))
                table.sort(sources)
                table.join2(candidates, sources)
            end
        end
    end
    if request.analysis then
        table.insert(files, path.join(request.root, "carven", "manifest.json"))
    end
    return input_paths(files), {request.argv, request.curdir or os.projectdir(), candidates}
end

-- Publish semantic artifacts or generated C++; Xmake owns native compilation.
function generate(target, request, opt)
    local option = import("core.base.option")
    local progress = import("utils.progress")
    local shared_label = target:data("carven.progress_label")
    local label = request.label or shared_label or source_label(target:data("carven.sources"))
    if option.get("dry-run") then
        print(os.args(table.join(request.program, request.argv)))
        return
    end
    local manifest = path.join(request.root, "carven", "manifest.json")
    local facts
    local function publish()
        -- Shared producers have internal names; keep those out of user-facing progress.
        if shared_label then
            if type(opt.progress) == "table" and opt.progress.set then
                opt.progress:set("target_name", nil)
            end
        else
            progress.set_target(opt.progress, target)
        end
        progress.show(opt.progress or 0, request.analysis and "${color.build.object}checking.cv %s"
            or "${color.build.object}generating.cpp %s", label)
        try {
            function ()
                os.tryrm(request.staging)
                os.mkdir(request.staging)
                run(request.program, request.argv, request.timings, label, request.curdir)
                facts = read_manifest(path.join(request.staging, "carven", "manifest.json"))
                local paths = {}
                for _, artifact in ipairs(facts.artifacts) do table.insert(paths, artifact.path) end
                table.insert(paths, "carven/manifest.json")
                local desired = {}
                for _, logical in ipairs(paths) do
                    local destination = path.join(request.root, logical)
                    desired[logical] = true
                    os.mkdir(path.directory(destination))
                    os.cp(path.join(request.staging, logical), destination, {copy_if_different = true})
                end
                for _, file in ipairs(os.files(path.join(request.root, "**"))) do
                    if not desired[path.unix(path.relative(file, request.root))] then os.tryrm(file) end
                end
            end,
            finally {
                function (ok, errors)
                    os.tryrm(request.staging)
                    if not ok then raise(errors) end
                end
            }
        }
    end
    facts = os.isfile(manifest) and read_manifest(manifest) or nil
    local files, values = input_state(request, facts)
    local depend = import("core.project.depend")
    depend.on_changed(function ()
        publish()
        local updated_files, updated_values = input_state(request, facts)
        table.clear(files)
        table.join2(files, updated_files)
        table.clear(values)
        table.join2(values, updated_values)
        os.mkdir(path.directory(request.dependfile))
    end, {
        dependfile = request.dependfile,
        files = files,
        values = values,
        changed = opt.rebuild or target:is_rebuilt() or not facts
            or request.analysis and not facts.cacheable,
    })
end

-- Reuse each script's analysis, but execute it on every build to observe host inputs.
function query_crafts(target, request, opt)
    if import("core.base.option").get("dry-run") then return end
    local config = import("core.project.config")
    local scheduler = import("core.base.scheduler")
    local manifest = path.join(request.root, "carven", "manifest.json")
    local facts = read_manifest(manifest)
    local queries = {}
    json.mark_as_array(queries)
    for _, directory in ipairs(facts.craft_roots) do
        local source = path.join(directory, "build.cv")
        if os.isfile(source) then
            local key = hash.uuid4(string.serialize({request.program, directory})):lower()
            local root = path.absolute(path.join(config.builddir(), ".carven", "queries", key), os.projectdir())
            local staging = root .. ".staging"
            local argv = {"check", "build.cv", "-o", staging}
            if request.timings then table.insert(argv, "--timings") end
            -- Keep publication and reads of shared checked artifacts together.
            -- Each consumer still executes its own query.
            scheduler.co_lock(root)
            try {
                function ()
                    generate(target, {
                        program = request.program,
                        root = root,
                        staging = staging,
                        argv = argv,
                        inputs = {source, request.program},
                        analysis = true,
                        dependfile = root .. ".depend",
                        curdir = directory,
                        label = path.relative(source, os.projectdir()),
                        timings = request.timings,
                    }, opt)
                    local arguments = {"query", root}
                    if request.timings then table.insert(arguments, "--timings") end
                    local output, errors = os.iorunv(request.program, arguments, {curdir = directory})
                    if errors and errors ~= "" then io.stderr:write(errors) end
                    table.insert(queries, assert(json.decode(output)))
                end,
                finally {
                    function (ok, errors)
                        scheduler.co_unlock(root)
                        if not ok then raise(errors) end
                    end,
                }
            }
        end
    end
    facts.crafts = queries
    local text = json.encode(facts)
    if text ~= io.readfile(manifest) then
        io.writefile(manifest .. ".tmp", text)
        os.mv(manifest .. ".tmp", manifest)
    end
end
