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
    }
end

local function integer(value, fallback, minimum, message)
    if value == nil then value = fallback end
    local number = (type(value) == "string" or type(value) == "number") and tonumber(value)
    assert(number and number >= minimum and number < math.huge and number == math.floor(number), message)
    return number
end

function normalize(name, provided, arguments)
    assert(name == "compile" or name == "incremental",
        "select compile or incremental: ./xmakew bench <name>")
    local settings = table.copy(provided or {})
    local options = definitions()
    table.insert(options, {"v", "verbose", "k", nil, "Show individual samples, warmups and case details"})
    table.insert(options, {"h", "help", "k", nil, "Show benchmark help without building"})
    local trailing = option.raw_parse(arguments or {}, options, {populate_defaults = false})
    for key, value in pairs(trailing) do settings[key] = value end
    settings.name = name
    if settings.help then return settings end
    settings.samples = integer(settings.samples, 3, 1, "samples must be a positive integer")
    settings.warmups = integer(settings.warmups, 1, 0, "warmups must be a nonnegative integer")
    for _, key in ipairs({"compiler", "case", "output"}) do
        if settings[key] ~= nil then
            assert(type(settings[key]) == "string" and #settings[key] > 0,
                key .. " must not be empty")
        end
    end
    return settings
end

function configure()
    local provided = {}
    for _, key in ipairs({"samples", "warmups", "compiler", "verbose", "list", "case", "output", "timings"}) do
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
