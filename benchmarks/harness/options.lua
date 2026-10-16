import("core.base.option")

local topic_settings = {
    {name = "compile", samples = 3},
    {name = "incremental", samples = 3},
    {name = "async", samples = 7},
    {name = "simd", samples = 7},
    {name = "json", samples = 7},
}

local specifications = {
    {name = "samples", kind = "kv", description = "Number of measured runs", minimum = 1},
    {name = "warmups", kind = "kv", description = "Number of warmup runs", default = 1, minimum = 0},
    {name = "compiler", kind = "kv", description = "Use another built Carven executable", topics = {compile = true}},
    {name = "list", kind = "k", description = "List cases without building or measuring"},
    {name = "case", kind = "kv", description = "Run one case by its exact identifier"},
    {name = "output", kind = "kv", description = "Save metadata and raw samples to a JSON file"},
    {name = "timings", kind = "k", description = "Collect Carven stage timings", topics = {compile = true, incremental = true}},
    {name = "command", kind = "kv", description = "Compiler command to measure", default = "compile",
        values = {"check", "compile"}, topics = {compile = true}},
    {name = "portable", kind = "k", description = "Force the portable SIMD backend", topics = {simd = true, json = true}},
    {name = "iterations", kind = "kv", description = "Execution work budget", default = 200000, minimum = 1, maximum = 10000000,
        topics = {async = true, simd = true}},
    {name = "depth", kind = "kv", description = "Async timed chain depth", default = 1024, minimum = 1, maximum = 1000000,
        topics = {async = true}},
    {name = "stack-depth", kind = "kv", description = "Async stack verification depth", default = 16384, minimum = 512, maximum = 1000000,
        topics = {async = true}},
    {name = "seed", kind = "kv", description = "Async workload seed", default = 42, minimum = 0, maximum = 4294967295,
        topics = {async = true}},
}

function topics()
    local names = {}
    for _, topic in ipairs(topic_settings) do table.insert(names, topic.name) end
    return names
end

local function topic_named(name)
    for _, topic in ipairs(topic_settings) do
        if topic.name == name then return topic end
    end
end

local function applies(specification, topic)
    return not specification.topics or specification.topics[topic] == true
end

local function default_value(specification, topic)
    return specification.name == "samples" and topic.samples or specification.default
end

function definitions(name)
    local topic = name and topic_named(name)
    local definitions = {}
    for _, specification in ipairs(specifications) do
        if not topic or applies(specification, topic.name) then
            local description = specification.description
            local default = topic and default_value(specification, topic) or specification.default
            if default ~= nil then
                description = description .. " (default: " .. default .. ")"
            elseif specification.name == "samples" then
                local defaults = {}
                for _, item in ipairs(topic_settings) do table.insert(defaults, item.name .. "=" .. item.samples) end
                description = description .. " (defaults: " .. table.concat(defaults, ", ") .. ")"
            end
            table.insert(definitions, {nil, specification.name, specification.kind, nil, description,
                values = specification.values})
        end
    end
    return definitions
end

function menu()
    local entries = definitions()
    table.insert(entries, {nil, "name", "v", nil, "Benchmark to run", values = topics()})
    table.insert(entries, {nil, "arguments", "vs", nil, "Benchmark options after the name"})
    return entries
end

function normalize(name, provided, arguments)
    local topic = topic_named(name)
    assert(topic, "select %s: ./xmakew bench <name>", table.concat(topics(), ", "))
    local settings = table.copy(provided or {})
    local options = definitions()
    table.insert(options, {"v", "verbose", "k", nil, "Show individual samples, warmups and case details"})
    table.insert(options, {"h", "help", "k", nil, "Show benchmark help without building"})
    local trailing = option.raw_parse(arguments or {}, options, {populate_defaults = false})
    for key, value in pairs(trailing) do settings[key] = value end
    settings.name = name
    if settings.help then return settings end
    for _, specification in ipairs(specifications) do
        local key, value = specification.name, settings[specification.name]
        if not applies(specification, name) then
            assert(value == nil or value == false, "--%s does not apply to %s benchmarks", key, name)
        elseif specification.minimum then
            if value == nil then value = default_value(specification, topic) end
            local number = (type(value) == "string" or type(value) == "number") and tonumber(value)
            assert(number and number >= specification.minimum and number < math.huge
                and number == math.floor(number) and (not specification.maximum or number <= specification.maximum),
                "--%s must be an integer in [%s, %s]", key, specification.minimum, specification.maximum or "unbounded")
            settings[key] = number
        elseif specification.kind == "kv" then
            if value == nil then value = default_value(specification, topic) end
            if value ~= nil then
                assert(type(value) == "string" and #value > 0, "--%s must not be empty", key)
                if specification.values then
                    assert(table.contains(specification.values, value), "--%s must be one of: %s",
                        key, table.concat(specification.values, ", "))
                end
                settings[key] = value
            end
        end
    end
    return settings
end

function configure()
    local provided = {verbose = option.get("verbose")}
    for _, specification in ipairs(specifications) do
        provided[specification.name] = option.get(specification.name)
    end
    local settings = normalize(option.get("name"), provided, option.get("arguments"))
    if settings.help then
        print("Usage: xmake bench %s [options]", settings.name)
        option.show_options(definitions(settings.name), "bench")
        return nil
    end
    return settings
end
