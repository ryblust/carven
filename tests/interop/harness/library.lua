function main(target)
    import("core.project.project")
    import("core.tool.compiler")
    import("core.tool.linker")
    local root = os.projectdir()
    local temporary = os.tmpfile() .. "-library"
    local fixture = path.join(root, "tests", "interop", "libraries")
    os.mkdir(temporary)
    os.cp(path.join(fixture, "*.cv"), temporary)
    local carven = path.absolute(project.target("carven"):targetfile())
    local producer, consumer = path.join(temporary, "producer"), path.join(temporary, "consumer")
    for _, output in ipairs({producer, consumer}) do
        local domain = output == producer and "interop:producer" or "interop:consumer"
        local args = {"compile", "--explicit-inputs", "--library-domain=interop:library",
            "--library-module=library", "--linkage-domain=" .. domain, "-o", output, "library.cv"}
        table.insert(args, output == producer and "--emit-library" or "consumer.cv")
        os.vrunv(carven, args, {curdir = temporary, timeout = 30000})
    end
    local objects = {}
    for index, source in ipairs({
        path.join(producer, "library.cpp"),
        path.join(consumer, "consumer.cpp"),
        path.join(fixture, "main.cpp"),
    }) do
        local object = path.join(temporary, tostring(index) .. ".o")
        local includes = {index == 1 and producer or consumer, path.join(root, "crafts")}
        local program, args = compiler.compargv(source, object, {
            target = target, configs = {includedirs = includes},
        })
        os.vrunv(program, args, {timeout = 30000})
        table.insert(objects, object)
    end
    local binary = path.join(temporary, is_host("windows") and "consumer-test.exe" or "consumer-test")
    local program, args = linker.linkargv("binary", {"cxx"}, objects, binary, {target = target})
    os.vrunv(program, args, {timeout = 30000})
    os.vrunv(binary, {}, {timeout = 30000})
    os.rm(temporary)
    return true
end
