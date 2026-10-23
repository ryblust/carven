import("core.project.project")
import("core.project.target", {alias = "target_api"})
import("private.utils.target", {alias = "target_utils"})

local directory = os.scriptdir()
local generation = import("generation", {rootdir = directory})
local batches = import("batches", {rootdir = directory})
local dependencies = import("dependencies", {rootdir = directory})

-- One checked input set and generation mode owns one generated C++ artifact set.
local function source_producer(consumer, checked, opt)
    local key = hash.uuid4(string.serialize({
        checked:fullname(), consumer:data("carven.tests") or false
    })):lower()
    local name = "carven-generated-" .. key
    local node = project.target(name)
    if node then
        if consumer:data("carven.timings") then node:data_set("carven.timings", true) end
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
    node._RULES = {}
    node:data_set("carven.program", consumer:data("carven.program"))
    node:data_set("carven.progress_label", generation.source_label(consumer:data("carven.sources")))
    node:data_set("carven.sources", consumer:data("carven.sources"))
    node:data_set("carven.batch_target", checked:fullname())
    node:data_set("carven.tests", consumer:data("carven.tests"))
    node:data_set("carven.timings", consumer:data("carven.timings"))
    node:data_set("carven.generated_root",
        path.absolute(path.join(node:autogendir(), "rules", "carven"), os.projectdir()))
    node:add("deps", checked:fullname(), {inherit = false, links = false})
    node:set("prepare", function (target, opt) batches.generate(target, opt) end)
    assert(node:_load())
    assert(node:_load_after())
    target_utils.config_target(node, opt)
    project.target_add(node)
    return node
end

-- The shared producer owns C++ generation. Consumers compile those sources
-- directly, so Xmake keeps their final settings, dependencies and object paths.
function attach(target, checked, opt)
    local sources = source_producer(target, checked, opt)
    target:add("deps", sources:fullname(), {inherit = false, links = false})
    target:data_set("carven.generation_target", sources:fullname())
    target:data_set("carven.dependency_files", {})
    return sources
end

-- Select generated and handwritten implementations through the same source graph.
function select_implementations(target)
    local option = import("core.base.option")
    if option.get("dry-run") then return end
    local root = target:data("carven.generated_root")
    local manifest = generation.read_manifest(path.join(root, "carven", "manifest.json"))
    target:set("files", generation.without_paths(target:get("files"), target:data("carven.dependency_files")))
    local files = dependencies.select(target, manifest)
    target:add("files", files)
    target:data_set("carven.dependency_files", files)
    generation.refresh_dependencies(target)
end
