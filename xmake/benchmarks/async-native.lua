function project_text(defines, implementations, root, include_root, cxx)
    local lines = {"set_project(\"carven-async-benchmark\")", "set_languages(\"c++20\")",
        "set_exceptions(\"cxx\")", "add_cxxflags(\"-fno-rtti\")",
        "if is_plat(\"windows\") then",
        "    set_toolchains(\"mingw[clang]\")",
        "    set_runtimes(\"c++_shared\")",
        "else",
        "    set_toolchains(\"llvm\")",
        "    if is_plat(\"linux\") then",
        "        set_runtimes(\"c++_shared\")",
        "        add_syslinks(\"c++abi\")",
        "    end",
        "end"}
    local function call(name, arguments)
        local values = {}
        for _, value in ipairs(arguments) do table.insert(values, string.format("%q", value)) end
        table.insert(lines, "    " .. name .. "(" .. table.concat(values, ", ") .. ")")
    end
    for _, implementation in ipairs(implementations) do
        for _, profile in ipairs({"timing", "allocations", "stack", "noexceptions"}) do
            if profile ~= "noexceptions" or implementation == "carven" then
                table.insert(lines, string.format("target(%q)", implementation .. "-" .. profile))
                table.insert(lines, "    set_kind(\"binary\")")
                table.insert(lines, "    set_default(false)")
                if cxx then
                    call("set_toolset", {"cxx", cxx})
                    call("set_toolset", {"ld", cxx})
                end
                table.insert(lines, "    set_optimize(" .. (profile == "stack" and "\"none\"" or "\"fastest\"") .. ")")
                if profile == "stack" then call("add_cxxflags", {"-fno-optimize-sibling-calls"})
                else call("add_defines", {"NDEBUG"}) end
                if profile == "noexceptions" then table.insert(lines, "    set_exceptions(\"no-cxx\")") end
                call("add_defines", {"CARVEN_ASYNC_BENCH_ALLOCATIONS=" .. (profile == "allocations" and "1" or "0")})
                call("add_defines", defines)
                call("add_includedirs", {path.join(include_root, "benchmarks", "async"),
                    path.join(include_root, "crafts"), path.join(root, "generated")})
                call("add_files", {path.join(include_root, "benchmarks", "async", "probe.cpp")})
                if implementation == "carven" then
                    call("add_files", {path.join(root, "generated", "**.cpp")})
                else
                    local source = implementation == "handwritten_runtime" and "orchestration.cpp" or "native.cpp"
                    call("add_files", {path.join(include_root, "benchmarks", "async", source)})
                    call("add_defines", {"CARVEN_ASYNC_BENCH_LIBCORO=" .. (implementation == "libcoro" and "1" or "0")})
                    if implementation == "libcoro" then
                        call("add_includedirs", {path.join(include_root, "libcoro", "include")})
                    end
                end
                table.insert(lines, "target_end()")
            end
        end
    end
    return table.concat(lines, "\n") .. "\n"
end

-- Query target outputs and actual native tool selection instead of reconstructing
-- executable locations or compiler invocations in the driver.
function describe_script()
    return [[
local config = import("core.project.config")
local project = import("core.project.project")
local json = import("core.base.json")
local find_tool = import("lib.detect.find_tool")
local compiler = import("core.tool.compiler")
local linker = import("core.tool.linker")
config.load()
local result = {}
for name, target in pairs(project.targets()) do
    local program, kind = target:tool("cxx")
    local tool = find_tool(kind, {program = program, version = true})
    local ld_program, ld_kind = target:tool("ld")
    local ld_tool = find_tool(ld_kind, {program = ld_program, version = true})
    local cxx = compiler.load("cxx", {target = target})
    local ld = linker.load(target:kind(), target:sourcekinds(), {target = target})
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
print(json.encode(result))
]]
end
