local generation = import("generation", {rootdir = os.scriptdir()})

-- Replace only additions owned by this rule; user target settings stay intact.
local function replace_owned(target, name, values)
    local key = "carven.native_" .. name
    local previous = target:data(key) or {}
    local without = name == "includedirs" and generation.without_paths or generation.without_values
    for _, value in ipairs(previous) do target:extraconf_set(name, value, {}) end
    if #previous > 0 then target:set(name, without(target:get(name), previous)) end

    local present, added = {}, {}
    local function identity(value)
        return name == "includedirs" and path.absolute(value, os.projectdir()) or value
    end
    for _, value in ipairs(table.wrap(target:get(name))) do present[identity(value)] = true end
    for _, value in ipairs(values) do
        if not present[identity(value)] then
            present[identity(value)] = true
            table.insert(added, value)
        end
    end
    if #added > 0 then target:add(name, added, {public = true}) end
    target:data_set(key, added)
end

local function platform(target)
    if target:is_plat("windows", "mingw") then return "windows" end
    if target:is_plat("macosx") then return "macos" end
    if target:is_plat("linux") then return "linux" end
end

function select(target, manifest)
    local canonical = {}
    local function identity(filename)
        local absolute = path.absolute(filename, os.projectdir())
        if not canonical[absolute] then canonical[absolute] = generation.physical_path(absolute) end
        return canonical[absolute]
    end

    replace_owned(target, "includedirs", manifest.include_dirs)

    local root = target:data("carven.generated_root")
    local implementations, generated = {}, {}
    for _, artifact in ipairs(manifest.artifacts) do
        local filename = path.join(root, artifact.path)
        if artifact.role == "module-implementation" or artifact.role == "test-entry" then
            local key = identity(filename)
            local source = type(artifact.source) == "string"
                and identity(path.absolute(artifact.source, manifest.workspace.root)) or nil
            generated[key] = {source = source, dependencies = artifact.link_dependencies}
            if source then
                implementations[source] = implementations[source] or {}
                table.insert(implementations[source], filename)
            end
        end
    end

    local by_source = {}
    for _, query in ipairs(manifest.crafts) do
        local craft = query.project
        local names = {}
        for _, declared in ipairs(craft.targets) do
            assert(not names[declared.name], "carven: duplicate Craft target in " .. craft.root .. ": " .. declared.name)
            names[declared.name] = {declaration = declared, scope = names}
        end
        for _, declared in ipairs(craft.targets) do
            if declared.kind == "source-library" then
                local record = names[declared.name]
                for _, source in ipairs(declared.sources) do
                    local key = identity(source)
                    by_source[key] = by_source[key] or {}
                    table.insert(by_source[key], record)
                end
            end
        end
    end

    local roots, existing = {}, {}
    for _, sourcefile in ipairs(target:sourcefiles()) do
        local sourcekind = target:sourcekind_of(sourcefile)
        if path.extension(sourcefile) == ".cv" or sourcekind == "cc" or sourcekind == "cxx"
            or sourcekind == "mm" or sourcekind == "mxx" then
            table.insert(roots, sourcefile)
            existing[identity(sourcefile)] = sourcefile
        end
    end

    local selected, queued, sources, files, syslinks = {}, {}, {}, {}, {}
    local target_platform = platform(target)
    local require_target
    local function require_file(filename)
        local key = identity(filename)
        if queued[key] then return end
        queued[key] = true
        local file = existing[key] or path.relative(key, os.projectdir())
        local kind = target:sourcekind_of(file)
        assert(kind == "cc" or kind == "cxx" or kind == "mm" or kind == "mxx",
            "carven: implementation is not a C/C++ source: " .. filename)
        if not existing[key] then table.insert(files, key) end
        table.insert(sources, key)
    end
    local function require_source(filename, resolve_owner)
        local key = identity(filename)
        if resolve_owner then
            local owners = by_source[key] or {}
            local contextual = false
            for _, owner in ipairs(owners) do
                if selected[owner] then
                    contextual = true
                    break
                end
            end
            if not contextual then
                assert(#owners <= 1, "carven: source belongs to multiple Craft targets: " .. filename)
                if owners[1] then require_target(owners[1]) end
            end
        end
        if path.extension(filename) == ".cv" then
            local outputs = assert(implementations[key],
                "carven: Craft input has no generated implementation; import it or add it to the Carven inputs: " .. filename)
            for _, output in ipairs(outputs) do require_file(output) end
        else
            require_file(filename)
        end
    end
    require_target = function (record)
        if selected[record] then return end
        selected[record] = true
        local declared = record.declaration
        assert(declared.kind == "source-library",
            "carven: source library cannot depend on product target: " .. declared.name)
        for _, link in ipairs(declared.syslinks) do
            local name = link.platform
            assert(name == "all" or name == "windows" or name == "linux" or name == "macos",
                "carven: unknown native target platform in " .. declared.name .. ": " .. tostring(name))
            if name == "all" or name == target_platform then table.join2(syslinks, link.libraries) end
        end
        for _, source in ipairs(declared.sources) do require_source(source, false) end
        for _, name in ipairs(declared.dependencies) do
            require_target(assert(record.scope[name], "carven: unknown Craft dependency: " .. name))
        end
    end

    local function require_links(links)
        for _, logical in ipairs(links) do
            local filename = path.join(root, logical)
            local artifact = assert(generated[identity(filename)],
                "carven: missing generated implementation: " .. logical)
            if artifact.source then
                require_source(artifact.source, true)
            else
                require_file(filename)
            end
        end
    end
    for _, source in ipairs(roots) do require_source(source, true) end

    local cursor = 1
    while cursor <= #sources do
        local source = sources[cursor]
        cursor = cursor + 1
        local artifact = generated[source]
        if artifact then require_links(artifact.dependencies) end
    end
    replace_owned(target, "syslinks", syslinks)
    table.sort(files)
    return files
end
