function main(target, name, diagnostics)
    import("core.project.project")
    import("core.tool.compiler")
    local root = os.projectdir()
    local temporary = os.tmpfile() .. "-interop"
    local source = path.join(root, "tests", "interop", "rejections", name .. ".cv")
    local generated = path.join(temporary, "generated")
    os.mkdir(temporary)
    os.cp(source, path.join(temporary, "probe.cv"))
    os.vrunv(path.absolute(project.target("carven"):targetfile()),
        {"compile", "-o", generated, "probe.cv"}, {curdir = temporary, timeout = 30000})
    local program, arguments = compiler.compargv(path.join(generated, "probe.cpp"), path.join(temporary, "probe.o"), {
        target = target,
        configs = {includedirs = {generated, path.join(root, "tests", "interop"), path.join(root, "crafts")}},
    })
    local stderr = path.join(temporary, "stderr.log")
    local code, run_error = os.execv(program, arguments, {try = true, timeout = 30000,
        stdout = path.join(temporary, "stdout.log"), stderr = stderr})
    local errors = (io.readfile(stderr) or ""):gsub("\r\n", "\n")
    -- Clang and clang-cl spell source locations differently.
    errors = errors:gsub("%((%d+),(%d+)%)%s*:", ":%1:%2:")
    local context = name .. " (artifacts: " .. temporary .. "):\n" .. errors
    assert(code == 1, "expected native diagnostic rejection, got " .. tostring(code)
        .. " (" .. tostring(run_error) .. "): " .. context)
    local primary
    -- Type-query failures can precede the diagnostic at the actual expression.
    for message in errors:gmatch("probe%.cv:%d+:%d+: error: ([^\n]+)") do
        if message:find(diagnostics[1], 1, true) then
            primary = message
            break
        end
    end
    -- Standard-library template errors can attribute the call through a note.
    if not primary and diagnostics.note then
        for note in errors:gmatch("probe%.cv:%d+:%d+: note: ([^\n]+)") do
            if note:find(diagnostics.note, 1, true) then
                primary = errors:match(": error: ([^\n]+)")
                break
            end
        end
    end
    assert(primary, "missing diagnostic attributed to fixture: " .. context)
    assert(primary:find(diagnostics[1], 1, true), "unexpected native diagnostic: " .. context)
    assert(errors:find(diagnostics[2], 1, true), "missing diagnostic subject: " .. context)
    os.rm(temporary)
end
