import("benchmarks.harness.runner", {alias = "benchmark", rootdir = os.projectdir()})
import("benchmarks.harness.inputs", {alias = "input_snapshot", rootdir = os.projectdir()})
import("benchmarks.compile.workloads", {rootdir = os.projectdir()})

function main(options)
    local selected = benchmark.select(workloads.cases(), options)
    if not selected then return end
    local command = options.command or "compile"
    local session = benchmark.session("compile", options, {
        description = {
            "Loads the generated workload sources and the selected compiler's installed Crafts.",
            command == "compile"
                and "Includes process startup, complete analysis, C++ generation and fresh artifact writes."
                or "Includes process startup and complete analysis, including compile-time execution; no C++ generation.",
            "Each sample starts a new compiler process; no workspace analysis cache is retained.",
            "Excludes native C++ compilation and linking.",
        },
    })
    benchmark.run(session, selected, function (session, record, case, root)
        local inputs, paths, source_bytes = workloads.inputs(case), {}, 0
        for filename, source in table.orderpairs(inputs) do
            io.writefile(path.join(root, filename), source)
            table.insert(paths, filename)
            source_bytes = source_bytes + #source
        end
        record.inputs, record.compiler_command = inputs, command
        record.source_count, record.source_bytes = #paths, source_bytes
        record.source_selection = "workload-and-toolchain"
        record.output_mode = command == "compile" and "fresh-artifacts" or "none"
        return function (ordinal)
            local args = {command}
            if options.timings then table.insert(args, "--timings") end
            if command == "compile" then
                table.join2(args, {"-o", "out-" .. ordinal})
            end
            table.join2(args, paths)
            return benchmark.command(session.compiler, args, root,
                {target = not options.compiler and "carven" or nil,
                    timings = options.timings, capture_stdout = true})
        end
    end, function (session)
        input_snapshot.capture(session)
    end)
end
