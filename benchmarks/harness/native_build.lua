function project_header(name)
    return string.format([[set_project(%q)
set_languages("c++20")
set_exceptions("cxx")
if is_plat("windows") then
    set_toolchains("mingw[clang]")
    set_runtimes("c++_shared")
else
    set_toolchains("llvm")
    if is_plat("linux") then
        set_runtimes("c++_shared")
        add_syslinks("c++abi")
    end
end]], name)
end

function carven_rules(session, inputs)
    local consumer = import("xmake.consumer", {rootdir = os.projectdir()})
    local configuration, rules = consumer.configuration(session.compiler, path.join(inputs, "crafts"))
    session.results.metadata.rules_directory = rules
    return configuration
end

-- Query target outputs and actual native tool selection instead of reconstructing
-- executable locations or compiler invocations in the driver.
function describe_script()
    return [[
local config = import("core.project.config")
local project = import("core.project.project")
local json = import("core.base.json")
local find_tool = import("lib.detect.find_tool")
local linker = import("core.tool.linker")
config.load()
local result = {}
for name, target in pairs(project.targets()) do
    if target:is_binary() then
        local program, kind = target:tool("cxx")
        local tool = find_tool(kind, {program = program, version = true})
        local ld_program, ld_kind = target:tool("ld")
        local ld_tool = find_tool(ld_kind, {program = ld_program, version = true})
        local cxx = target:compiler("cxx")
        local ld = linker.load(target:kind(), {"cxx"}, {target = target})
        result[name] = {executable = path.absolute(target:targetfile()), cxx = program,
            cxx_kind = kind, cxx_version = tool and tool.version or "unobserved",
            ld = ld_program, ld_kind = ld_kind, ld_version = ld_tool and ld_tool.version or "unobserved",
            toolchains = target:get("toolchains"), runtimes = target:get("runtimes"),
            languages = target:get("languages"), optimize = target:get("optimize"),
            defines = target:get("defines"), cxxflags = target:get("cxxflags"),
            ldflags = target:get("ldflags"), syslinks = target:get("syslinks"),
            resolved_cxxflags = cxx:compflags(), resolved_ldflags = ld:linkflags(),
            exceptions = target:get("exceptions"), platform = target:plat(), arch = target:arch()}
    end
end
print(json.encode(result))
]]
end
