import("core.project.project")
import("lib.detect.find_tool")
import("core.tool.linker")

function main(namespace, names)
    local result = {}
    for _, name in ipairs(names) do
        local target = assert(project.target(namespace .. "::" .. name))
        local program, kind = target:tool("cxx")
        local tool = find_tool(kind, {program = program, version = true})
        local ld_program, ld_kind = target:tool("ld")
        local ld_tool = find_tool(ld_kind, {program = ld_program, version = true})
        local cxx = target:compiler("cxx")
        local ld = linker.load(target:kind(), {"cxx"}, {target = target})
        local source_flags = {}
        for _, batch in pairs(target:sourcebatches()) do
            if batch.sourcekind == "cxx" then
                for _, source in ipairs(batch.sourcefiles) do
                    source_flags[source] = cxx:compflags({sourcefile = source})
                end
            end
        end

        result[name] = {
            name = target:fullname(),
            executable = path.absolute(target:targetfile()),
            object_directory = path.absolute(target:objectdir()),
            generated_directory = path.absolute(target:data("carven.generated_root") or target:autogendir()),
            cxx = program,
            cxx_kind = kind, cxx_version = tool and tool.version or "unobserved",
            ld = ld_program, ld_kind = ld_kind, ld_version = ld_tool and ld_tool.version or "unobserved",
            toolchains = target:get("toolchains"), runtimes = target:get("runtimes"),
            languages = target:get("languages"), optimize = target:get("optimize"),
            defines = target:get("defines"), cxxflags = target:get("cxxflags"),
            ldflags = target:get("ldflags"), syslinks = target:get("syslinks"),
            resolved_cxxflags = cxx:compflags(), resolved_ldflags = ld:linkflags(),
            source_cxxflags = source_flags,
            exceptions = target:get("exceptions"), platform = target:plat(), arch = target:arch(),
        }
    end
    return result
end
