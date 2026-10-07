import("xmake.benchmarks.runner", {alias = "benchmark", rootdir = os.projectdir()})
import("benchmarks.compile.workloads", {rootdir = os.projectdir()})

function main(options)
    local selected = benchmark.select(workloads.cases(), options)
    if not selected then return end
    local session = benchmark.session("compile", options, {
        description = {
            "Includes process startup, analysis and C++ generation; excludes native C++ compilation.",
            "Module batches write fresh artifacts; structured cases send inspection output to the null device.",
        },
    })
    benchmark.run(session, selected, function (session, record, case, root)
        local inputs, paths = workloads.inputs(case), {}
        for filename, source in table.orderpairs(inputs) do
            io.writefile(path.join(root, filename), source)
            table.insert(paths, filename)
        end
        local domain = "benchmark:compile" .. (case.count and ":" .. case.count or "")
        record.inputs, record.linkage_domain = inputs, domain
        record.output_mode = case.count and "fresh-artifacts" or "inspection-to-null"
        return function (ordinal)
            local args = {"compile"}
            if options.timings then table.insert(args, "--timings") end
            if case.count then
                table.join2(args, {"--output-dir", "out-" .. ordinal})
            else
                table.insert(args, "--stdout")
            end
            table.insert(args, "--linkage-domain=" .. domain)
            table.join2(args, paths)
            return benchmark.command(session.compiler, args, root,
                {timings = options.timings, capture_stdout = case.count ~= nil})
        end
    end)
end
