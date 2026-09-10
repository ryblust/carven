function main(target)
    import("core.project.project")
    import("core.tool.compiler")
    local root = os.projectdir()
    local temporary = os.tmpfile() .. "-ptr"
    os.mkdir(temporary)
    local cases = {
        {name = "positive", succeeds = true,
         source = "fn probe(p: ptr<i32>) -> i32 { if p == nullptr { return 0; } return *p; }"},
        {name = "const-conversion", source = "fn probe() { let p: ptr<&::pointer_probe::Fixed> = ::pointer_probe::readonly_fixed(); }",
         reason = "cannot initialize", marker = "pointer_probe::readonly_fixed"},
        {name = "noncopyable-target", source = "fn probe(p: ptr<::pointer_probe::Fixed>) { if p == nullptr { return; } let value = *p; }",
         reason = "deleted constructor", subject = "Fixed"},
        {name = "native-double-output", source = "fn probe() { var p: ptr<&i32> = nullptr; ::pointer_probe::output(&p); }",
         reason = "cannot initialize a parameter", marker = "pointer_probe::output"},
        {name = "self-dependent-callable", source = "export struct Node { callback: ptr<fn(Node) -> void> }",
         reason = "incomplete type", subject = "Node", type_formation = true},
    }
    for _, case in ipairs(cases) do
        local directory = path.join(temporary, case.name)
        os.mkdir(directory)
        io.writefile(path.join(directory, "probe.cv"), 'import "pointers/provider.hpp";\n' .. case.source .. "\n")
        local generated = path.join(directory, "generated")
        os.vrunv(path.absolute(project.target("carven"):targetfile()), {"-o", generated, "probe.cv"}, {curdir = directory, timeout = 30000})
        local cpp = path.join(generated, "probe.cpp")
        local program, arguments = compiler.compargv(cpp, path.join(directory, "probe.o"), {
            target = target,
            configs = {includedirs = {generated, path.join(root, "tests", "interop"), path.join(root, "crafts")}},
        })
        local stdout_file = path.join(directory, "stdout.log")
        local stderr_file = path.join(directory, "stderr.log")
        local code = os.execv(program, arguments, {try = true, timeout = 30000, stdout = stdout_file, stderr = stderr_file})
        local errors = io.readfile(stderr_file) or ""
        if case.succeeds then
            assert(code == 0, "pointer rejection harness positive control failed:\n" .. errors)
        elseif case.type_formation then
            local primary = errors:match(": error: ([^\n]+)") or ""
            local reported = tonumber(errors:match("probe%.hpp:(%d+):%d+: note: [^\n]-'ReadArg'"))
            local header = io.readfile(path.join(generated, "carven", "generated", "probe.hpp"))
            local line_number, selected = 1, false
            for line in (header .. "\n"):gmatch("(.-)\n") do
                if line_number == reported and line:find("ReadArg<Node>", 1, true) then
                    selected = true
                end
                line_number = line_number + 1
            end
            assert(code ~= 0 and selected and primary:find(case.reason, 1, true) and primary:find(case.subject, 1, true),
                "self-dependent callable did not fail at its Read parameter representation:\n" .. errors)
        else
            local reported = tonumber(errors:match("probe%.cv:(%d+):%d+: error:") or errors:match("probe%.cv%((%d+),%d+%): error:"))
            local primary = errors:match("probe%.cv[^\n]-: error: ([^\n]+)") or ""
            local line_number, file, selected = 1, cpp,
                reported ~= nil and (
                    (case.subject ~= nil and primary:find(case.subject, 1, true) ~= nil)
                    or (case.marker ~= nil and primary:find(case.marker, 1, true) ~= nil)
                )
            for line in (io.readfile(cpp) .. "\n"):gmatch("(.-)\n") do
                local number, name = line:match('^#line (%d+) "([^"]+)"')
                if number then
                    line_number, file = tonumber(number), name
                else
                    if file == "probe.cv" and line_number == reported and case.marker and line:find(case.marker, 1, true) then
                        selected = true
                    end
                    line_number = line_number + 1
                end
            end
            assert(code ~= 0 and selected and primary:find(case.reason, 1, true),
                case.name .. " did not fail at its pointer operation for the expected C++ reason:\n" .. errors)
        end
    end
    os.rm(temporary)
end
