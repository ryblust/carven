import("core.base.option")
import("core.project.config")
import("core.project.project")

function settings()
    local samples = tonumber(option.get("samples"))
    local warmups = tonumber(option.get("warmups"))
    assert(samples and samples >= 1 and samples == math.floor(samples), "samples must be a positive integer")
    assert(warmups and warmups >= 0 and warmups == math.floor(warmups), "warmups must be a nonnegative integer")
    config.load()
    local compiler = option.get("compiler") or project.target("carven"):targetfile()
    compiler = path.absolute(compiler, os.projectdir())
    assert(os.isfile(compiler), "Carven compiler does not exist: %s; build it first", compiler)
    print("Compiler: %s", compiler)
    print("Sampling: %d warmup + %d measured runs", warmups, samples)
    print("Observational timings; compare the same machine and compiler build mode.")
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
