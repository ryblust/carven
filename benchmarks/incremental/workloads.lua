function cases()
    local library = "library.cv"
    local extra = "extra.cv.fixture"
    local public = "library_public_edit.cv.fixture"
    local baseline_stdout = "42\n"
    return {
        {id = "no_changes", size = "4 modules", label = "No changes", unchanged = true, stdout = baseline_stdout},
        {id = "identical_content_rewrite", size = "4 modules", label = "Identical content rewrite", baseline = {["library.cv"] = library},
            edits = {["library.cv"] = library}, unchanged = true, stdout = baseline_stdout},
        {id = "private_function_edit", size = "4 modules", label = "Private function edit", baseline = {["library.cv"] = library},
            edits = {["library.cv"] = "library_private_edit.cv.fixture"}, stdout = "43\n"},
        {id = "public_interface_edit", size = "4 modules", label = "Public interface edit", baseline = {["library.cv"] = library},
            edits = {["library.cv"] = public}, stdout = baseline_stdout},
        {id = "add_module", size = "4 -> 5 modules", label = "Add module", baseline = {["extra.cv"] = false},
            edits = {["extra.cv"] = extra}, stdout = baseline_stdout},
        {id = "remove_module", size = "5 -> 4 modules", label = "Remove module", baseline = {["extra.cv"] = extra},
            edits = {["extra.cv"] = false}, stdout = baseline_stdout},
    }
end

function inputs()
    local result = {["extra.cv"] = false}
    for _, name in ipairs({"library.cv", "facade.cv", "app.cv", "unrelated.cv"}) do
        result[name] = name
    end
    return result
end
