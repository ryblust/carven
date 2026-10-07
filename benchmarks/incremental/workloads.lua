local function source(name)
    return io.readfile(path.join(os.projectdir(), "benchmarks", "incremental", name))
end

function cases()
    local library = source("library.cv")
    local extra = source("extra.cv.fixture")
    local public = source("public_interface.cv.fixture")
    return {
        {id = "no_changes", size = "4 modules", label = "No changes"},
        {id = "identical_content_rewrite", size = "4 modules", label = "Identical content rewrite", baseline = {["library.cv"] = library},
            edits = {["library.cv"] = library}},
        {id = "private_function_edit", size = "4 modules", label = "Private function edit", baseline = {["library.cv"] = library},
            edits = {["library.cv"] = source("private_function.cv.fixture")}},
        {id = "public_interface_edit", size = "4 modules", label = "Public interface edit", baseline = {["library.cv"] = library},
            edits = {["library.cv"] = public}},
        {id = "add_module", size = "4 -> 5 modules", label = "Add module", baseline = {["extra.cv"] = false},
            edits = {["extra.cv"] = extra}, artifacts = {extra = 1}},
        {id = "remove_module", size = "5 -> 4 modules", label = "Remove module", baseline = {["extra.cv"] = extra},
            edits = {["extra.cv"] = false}, artifacts = {extra = 0}},
    }
end

function inputs()
    local result = {}
    for _, name in ipairs({"library.cv", "facade.cv", "app.cv", "unrelated.cv"}) do
        result[name] = source(name)
    end
    return result
end
