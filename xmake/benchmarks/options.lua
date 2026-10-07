import("core.base.option")

function definitions()
    return {
        {nil, "samples", "kv", nil, "Number of measured runs (default: 3)"},
        {nil, "warmups", "kv", "1", "Number of warmup runs"},
        {nil, "compiler", "kv", nil, "Use another Carven executable without building"},
        {nil, "list", "k", nil, "List cases without building or measuring"},
        {nil, "case", "kv", nil, "Run one case by its exact identifier"},
        {nil, "output", "kv", nil, "Save metadata and raw samples to a JSON file"},
        {nil, "timings", "k", nil, "Collect Carven stage timings"},
        {nil, "cxx", "kv", nil, "Native C++ compiler for async benchmarks"},
        {nil, "libcoro", "kv", nil, "Optional libcoro checkout for async comparison"},
        {nil, "iterations", "kv", nil, "Async work budget (default: 200000)"},
        {nil, "depth", "kv", nil, "Async timed chain depth (default: 1024)"},
        {nil, "stack-depth", "kv", nil, "Async stack verification depth (default: 16384)"},
        {nil, "seed", "kv", nil, "Async workload seed (default: 42)"},
    }
end

local function integer(value, fallback, minimum, message)
    if value == nil then value = fallback end
    local number = (type(value) == "string" or type(value) == "number") and tonumber(value)
    assert(number and number >= minimum and number < math.huge and number == math.floor(number), message)
    return number
end

function normalize(name, provided, arguments)
    assert(name == "compile" or name == "incremental" or name == "async",
        "select compile, incremental or async: ./xmakew bench <name>")
    local settings = table.copy(provided or {})
    local options = definitions()
    table.insert(options, {"v", "verbose", "k", nil, "Show individual samples, warmups and case details"})
    table.insert(options, {"h", "help", "k", nil, "Show benchmark help without building"})
    local trailing = option.raw_parse(arguments or {}, options, {populate_defaults = false})
    for key, value in pairs(trailing) do settings[key] = value end
    settings.name = name
    if settings.help then return settings end
    settings.samples = integer(settings.samples, name == "async" and 7 or 3, 1, "samples must be a positive integer")
    settings.warmups = integer(settings.warmups, 1, 0, "warmups must be a nonnegative integer")
    if name == "async" then
        settings.iterations = integer(settings.iterations, 200000, 1, "iterations must be in [1,10000000]")
        settings.depth = integer(settings.depth, 1024, 1, "depth must be in [1,1000000]")
        settings["stack-depth"] = integer(settings["stack-depth"], 16384, 512, "stack-depth must be in [512,1000000]")
        settings.seed = integer(settings.seed, 42, 0, "seed must be in [0,4294967295]")
        assert(settings.iterations <= 10000000, "iterations must be in [1,10000000]")
        assert(settings.depth <= 1000000 and settings["stack-depth"] <= 1000000, "depth must not exceed 1000000")
        assert(settings.seed <= 4294967295, "seed must be in [0,4294967295]")
    end
    for _, key in ipairs({"compiler", "case", "output", "cxx", "libcoro"}) do
        if settings[key] ~= nil then
            assert(type(settings[key]) == "string" and #settings[key] > 0,
                key .. " must not be empty")
        end
    end
    return settings
end

function configure()
    local provided = {}
    for _, key in ipairs({"samples", "warmups", "compiler", "verbose", "list", "case", "output", "timings", "cxx", "libcoro", "iterations", "depth", "stack-depth", "seed"}) do
        provided[key] = option.get(key)
    end
    local settings = normalize(option.get("name"), provided, option.get("arguments"))
    if settings.help then
        local menu = option.taskmenu("bench")
        print("Usage: %s", menu.usage)
        print("%s", menu.description)
        option.show_options(menu.options, "bench")
        return nil
    end
    return settings
end
