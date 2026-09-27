import("core.base.option")
import("core.project.config")
import("core.project.project")

function settings(title, default_samples)
    local samples = tonumber(option.get("samples") or default_samples)
    local warmups = tonumber(option.get("warmups"))
    assert(samples and samples >= 1 and samples < math.huge and samples == math.floor(samples), "samples must be a positive integer")
    assert(warmups and warmups >= 0 and warmups < math.huge and warmups == math.floor(warmups), "warmups must be a nonnegative integer")
    if not option.get("compiler") then
        print("Updating the configured compiler...")
        os.vrunv(os.programfile(), {"build", "carven"}, {curdir = os.projectdir()})
    end
    config.load()
    local compiler = option.get("compiler") or project.target("carven"):targetfile()
    compiler = path.absolute(compiler, os.projectdir())
    assert(os.isfile(compiler), "Carven compiler does not exist: %s; build it first", compiler)
    print("")
    print("%-15s %s", "Benchmark:", title)
    print("%-15s %s", "Compiler:", compiler)
    print("%-15s %s", "Compiler mode:", option.get("compiler") and "external (unknown)" or config.mode())
    print("%-15s %d warmup + %d measured runs", "Sampling:", warmups, samples)
    print("%-15s %s", "Time:", "median wall time in ms")
    return compiler, samples, warmups
end

function temporary(action)
    local root = os.tmpfile()
    os.mkdir(root)
    local failure
    local result = table.pack(try {
        function () return action(root) end,
        catch {function (errors) failure = errors end},
        finally {function () os.tryrm(root) end}
    })
    if failure then
        raise(failure)
    end
    return table.unpack(result, 1, result.n)
end

function measure(samples, warmups, action)
    local results = {}
    for ordinal = 1, warmups + samples do
        local started = os.mclock()
        action(ordinal)
        local elapsed = os.mclock() - started
        if ordinal > warmups then
            table.insert(results, elapsed)
        end
    end
    return results
end

function median(samples)
    local sorted = table.copy(samples)
    table.sort(sorted)
    local middle = math.floor(#sorted / 2)
    return #sorted % 2 == 1 and sorted[middle + 1] or (sorted[middle] + sorted[middle + 1]) / 2
end

function progress(index, total, label)
    print("[%d/%d] %s ...", index, total, label)
    io.flush()
end

function details(name, samples)
    if option.get("verbose") then
        local values = {}
        for _, value in ipairs(samples) do table.insert(values, string.format("%.2f", value)) end
        print("  %s: samples (ms): %s", name, table.concat(values, ", "))
    end
end
