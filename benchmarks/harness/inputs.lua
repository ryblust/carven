function crafts_root(compiler)
    while os.islink(compiler) do
        compiler = path.absolute(os.readlink(compiler), path.directory(compiler))
    end
    local directory = path.directory(compiler)
    local installed = path.join(path.directory(directory), "crafts")
    if os.isdir(path.join(installed, "carven")) then return installed end
    while directory ~= path.directory(directory) do
        if os.isfile(path.join(directory, "src/carven.cppm")) and os.isfile(path.join(directory, "xmake.lua"))
            and os.isdir(path.join(directory, "crafts/carven")) then
            return path.join(directory, "crafts")
        end
        directory = path.directory(directory)
    end
    raise("cannot locate the selected compiler's crafts")
end

function capture(session)
    local root = path.join(session.artifacts, "inputs")
    local manifest = {}
    local function record(file, relative, snapshot)
        if snapshot then
            local destination = path.join(root, relative)
            os.mkdir(path.directory(destination))
            os.cp(file, destination)
        end
        manifest[relative] = {source = file, sha256 = hash.sha256(file):lower(),
            bytes = os.filesize(file), snapshot = snapshot or false}
    end
    local function directory(source, relative, snapshot)
        for _, file in ipairs(os.files(path.join(source, "**"))) do
            record(file, path.join(relative, path.relative(file, source)), snapshot)
        end
    end
    directory(path.join(session.results.metadata.crafts_root, "carven"), "crafts/carven")
    record(path.join(os.projectdir(), "xmake.lua"), "xmake.lua", true)
    local carven_package = import("core.project.project").required_package("carven")
    directory(path.join(carven_package:installdir(), "rules"), "rules", true)
    local topic = path.join("benchmarks", session.results.topic)
    directory(path.join(os.projectdir(), topic), topic, true)
    directory(path.join(os.projectdir(), "benchmarks/harness"), "benchmarks/harness", true)
    if session.options.libcoro then
        directory(path.join(session.options.libcoro, "include"), "libcoro/include")
    end
    session.results.metadata.inputs = {directory = root, files = manifest}
    return root
end

function finish(session)
    local inputs = session.results.metadata.inputs
    for relative, entry in pairs(inputs.files) do
        assert(hash.sha256(entry.source):lower() == entry.sha256,
            "benchmark input changed during measurement: %s", entry.source)
        if entry.snapshot then
            assert(hash.sha256(path.join(inputs.directory, relative)):lower() == entry.sha256,
                "benchmark input snapshot changed: %s", relative)
        end
    end
    local generated = {}
    for _, file in ipairs(os.files(path.join(session.artifacts, "**"))) do
        local relative = path.relative(file, session.artifacts)
        local extension = path.extension(file)
        if not inputs.files[path.relative(file, inputs.directory)]
            and (extension == ".cpp" or extension == ".hpp" or extension == ".cv" or extension == ".lua") then
            generated[relative] = {sha256 = hash.sha256(file):lower(), bytes = os.filesize(file)}
        end
    end
    session.results.metadata.generated_sources = generated
end
