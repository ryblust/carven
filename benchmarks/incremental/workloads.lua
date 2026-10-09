local function source(name, root)
    return io.readfile(path.join(root or os.projectdir(), "benchmarks", "incremental", name))
end

function cases(root)
    local library = source("library.cv", root)
    local extra = source("extra.cv.fixture", root)
    local public = source("library_public_edit.cv.fixture", root)
    local baseline_stdout = "42\n"
    return {
        {id = "no_changes", size = "4 modules", label = "No changes", stdout = baseline_stdout},
        {id = "identical_content_rewrite", size = "4 modules", label = "Identical content rewrite", baseline = {["library.cv"] = library},
            edits = {["library.cv"] = library}, stdout = baseline_stdout},
        {id = "private_function_edit", size = "4 modules", label = "Private function edit", baseline = {["library.cv"] = library},
            edits = {["library.cv"] = source("library_private_edit.cv.fixture", root)}, stdout = "43\n"},
        {id = "public_interface_edit", size = "4 modules", label = "Public interface edit", baseline = {["library.cv"] = library},
            edits = {["library.cv"] = public}, stdout = baseline_stdout},
        {id = "add_module", size = "4 -> 5 modules", label = "Add module", baseline = {["extra.cv"] = false},
            edits = {["extra.cv"] = extra}, artifacts = {extra = 1}, stdout = baseline_stdout},
        {id = "remove_module", size = "5 -> 4 modules", label = "Remove module", baseline = {["extra.cv"] = extra},
            edits = {["extra.cv"] = false}, artifacts = {extra = 0}, stdout = baseline_stdout},
    }
end

function inputs(root)
    local result = {}
    for _, name in ipairs({"library.cv", "facade.cv", "app.cv", "unrelated.cv"}) do
        result[name] = source(name, root)
    end
    return result
end
