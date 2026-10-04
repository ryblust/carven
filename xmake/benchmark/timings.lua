-- Each report belongs to one Carven invocation. Keep rounded values and bounds
-- verbatim; observations from different invocations must not overwrite each other.
local function duration(raw)
    local bound, value, unit = raw:match("^(<?)([%d%.]+)%s+(%a+)$")
    value = tonumber(value)
    if not value or (unit ~= "ms" and unit ~= "s") then return nil end
    local result = {raw = raw}
    value = unit == "s" and value * 1000 or value
    if bound == "<" then
        result.upper_bound_ms, result.exclusive = value, true
    else
        result.ms = value
    end
    return result
end

function parse(stderr)
    local reports, current = {}, nil
    for line in (stderr .. "\n"):gmatch("([^\n]*)\n") do
        line = line:gsub("\r$", "")
        local label, raw = line:match("^carven: (.-) in (.-)%s*$")
        local total = raw and duration(raw)
        if total then
            current = {label = label, total = total, stages = {}}
            table.insert(reports, current)
        elseif current then
            local heading = line:match("^%s+Stage%s+Time%s+%% total%s*$")
                or line:match("^%s+%-+%s+%-+%s+%-+%s*$")
            if not heading then
                local stage, elapsed, share = line:match(
                    "^%s+([^%s].-)%s%s+(<?[%d%.]+%s+%a+)%s+(<?[%d%.]+%%)%s*$")
                if not stage then
                    stage, elapsed = line:match("^%s+([^%s].-)%s%s+(.-)%s*$")
                end
                local measured = elapsed and duration(elapsed)
                if measured then
                    table.insert(current.stages, {label = stage, duration = measured, share_raw = share})
                else
                    current = nil
                end
            end
        end
    end
    return {status = #reports > 0 and "reported" or "unreported", reports = reports}
end
