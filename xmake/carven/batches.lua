import("core.project.config")
import("core.project.project")
import("core.project.target", {alias = "target_api"})
import("private.utils.target", {alias = "target_utils"})

local directory = os.scriptdir()
local generation = import("generation", {rootdir = directory})

function invocation(target)
    if #(target:data("carven.sources") or {}) == 0 then return nil end
    local producer = assert(project.target(target:data("carven.batch_target")))
    local batch = producer:data("carven.batch_root")
    local root = target:data("carven.generated_root")
    local staging = root .. ".staging"
    local argv = {"compile", batch, "--no-query", "-o", staging}
    if target:data("carven.timings") then
        table.insert(argv, "--timings")
    end
    local tests = target:data("carven.tests")
    if tests == true then
        table.insert(argv, "--tests")
    elseif tests == "runner" then
        table.insert(argv, "--tests=runner")
    end
    return {
        program = target:data("carven.program"),
        root = root,
        staging = staging,
        argv = argv,
        inputs = {target:data("carven.program"), path.join(batch, "carven", "semantic")},
        dependfile = root .. ".depend",
        timings = target:data("carven.timings"),
    }
end

function generate(target, opt)
    local request = invocation(target)
    if not request then return end
    generation.generate(target, request, opt)
    generation.query_crafts(target, request, opt)
end

local function prepare(node, opt)
    local root = node:data("carven.batch_root")
    local staging = root .. ".staging"
    local argv = {"check"}
    local inputs = table.join(node:data("carven.sources"), {node:data("carven.program")})
    table.join2(argv, node:data("carven.source_arguments"))
    table.join2(argv, {"-o", staging})
    if node:data("carven.timings") then
        table.insert(argv, "--timings")
    end
    generation.generate(node, {
        program = node:data("carven.program"),
        root = root,
        staging = staging,
        argv = argv,
        inputs = inputs,
        analysis = true,
        dependfile = root .. ".depend",
        timings = node:data("carven.timings"),
    }, opt)
end

local function producer(consumer, sources, arguments, opt)
    local program = consumer:data("carven.program")
    local identity = table.join({program}, arguments)
    local key = hash.uuid4(string.serialize(identity)):lower()
    local name = "carven-batch-" .. key
    local node = project.target(name)
    if node then
        if consumer:data("carven.timings") then
            node:data_set("carven.timings", true)
        end
        return node
    end
    local info = assert(table.values(project.scope("target"))[1]):clone()
    for key in pairs(info:info()) do info:set(key, nil) end
    info:set("__scriptdir", os.projectdir())
    node = target_api.new(name, info)
    node:set("kind", "phony")
    node:set("default", false)
    node:set("group", "carven/generation")
    node:set("policy", "build.fence", true)
    -- Match Xmake's target loader for a target with no source or target rules.
    node._RULES = {}
    node:set("prepare", prepare)
    node:data_set("carven.program", program)
    node:data_set("carven.sources", sources)
    node:data_set("carven.progress_label", generation.source_label(sources))
    node:data_set("carven.source_arguments", arguments)
    node:data_set("carven.timings", consumer:data("carven.timings"))
    node:data_set("carven.batch_root",
        path.absolute(path.join(config.builddir(), ".carven", "batches", key), os.projectdir()))
    assert(node:_load())
    assert(node:_load_after())
    target_utils.config_target(node, opt)
    project.target_add(node)
    return node
end

function attach(target, opt)
    local node = producer(
        target,
        target:data("carven.sources"),
        target:data("carven.source_arguments"),
        opt
    )
    target:data_set("carven.batch_target", node:fullname())
    local generator = import("artifacts", {rootdir = directory}).attach(target, node, opt)
    return generator:data("carven.generated_root")
end
