includes("workloads.lua")
local sources = inputs()
local fixtures = os.scriptdir()
local root = path.absolute(path.join(get_config("builddir") or get_config("buildir") or "build",
    "benchmarks/incremental/workload"), os.projectdir())

namespace("benchmarks::incremental", function ()
    target("workload")
        set_default(false)
        set_kind("binary")
        set_group("benchmarks/incremental")
        set_rundir(root)
        set_languages("c++20")
        set_optimize("none")
        set_symbols("debug")
        add_rules("@carven/carven", {timings = true})
        add_files(path.join(root, "*.cv"))
        on_load(function (target)
            if not os.isdir(root) then
                os.mkdir(root)
                for name, source in pairs(sources) do
                    if source then
                        io.writefile(path.join(root, name), io.readfile(path.join(fixtures, source)))
                    end
                end
            end
        end)
    target_end()
end)
