local pipe = import("core.base.pipe")
local process = import("core.base.process")
local bytes = import("core.base.bytes")

local function integer(value, width)
    local result = {}
    for index = 1, width do
        result[index] = string.char(value % 256)
        value = math.floor(value / 256)
    end
    return table.concat(result)
end

local function text(value)
    return integer(#value, 4) .. value
end

local function frame(payload)
    return integer(#payload, 4) .. payload
end

local function update(document, version, source)
    return string.char(1) .. text(document) .. integer(version, 8) .. text(source)
end

local function query(tag, document, offset)
    return string.char(tag) .. text(document) .. integer(offset, 4)
end

local function decode(payload)
    local pos = 1
    local function uint(width)
        assert(pos + width - 1 <= #payload, "truncated response field")
        local result, scale = 0, 1
        for index = 1, width do
            result = result + payload:byte(pos) * scale
            pos = pos + 1
            scale = scale * 256
        end
        return result
    end
    local function string_value()
        local length = uint(4)
        assert(pos + length - 1 <= #payload, "truncated response string")
        local result = payload:sub(pos, pos + length - 1)
        pos = pos + length
        return result
    end
    local function list(read)
        local result = {}
        for index = 1, uint(4) do
            result[index] = read()
        end
        return result
    end
    local function optional(read)
        local present = uint(1)
        assert(present == 0 or present == 1, "invalid response presence")
        if present == 1 then
            return read()
        end
    end
    local function boolean()
        local value = uint(1)
        assert(value == 0 or value == 1, "invalid response boolean")
        return value == 1
    end
    local function location()
        return {document = string_value(), version = uint(8), start = uint(4), finish = uint(4)}
    end
    local function label()
        return {location = location(), message = string_value()}
    end
    local result = {tag = uint(1)}
    result.document_versions = list(function ()
        return {document = string_value(), version = uint(8)}
    end)
    if result.tag == 0 then
        result.code = string_value()
        result.message = string_value()
    elseif result.tag == 1 then
        assert(#result.document_versions == 0, "acknowledgements have no version payload")
    elseif result.tag == 2 then
        result.published = boolean()
        result.diagnostics = list(function ()
            return {severity = string_value(), code = string_value(), message = string_value(),
                primary = optional(label), related = list(label),
                notes = list(function ()
                    return {location = optional(location), message = string_value()}
                end), helps = list(string_value)}
        end)
        result.output = list(function ()
            return {stream = string_value(), bytes = string_value()}
        end)
    elseif result.tag == 3 then
        result.hover = optional(function ()
            return {location = location(), type_text = string_value()}
        end)
    elseif result.tag == 4 then
        result.location = optional(location)
    elseif result.tag == 5 then
        result.locations = optional(function () return list(location) end)
    else
        raise("unknown response tag: %s", result.tag)
    end
    assert(pos == #payload + 1, "extra response bytes")
    return result
end

local function with_service(program, name, run)
    local pipes = {}
    local child, failure, completed
    local reaped = false
    local stderr = os.tmpfile()
    local function close_pipe(name)
        if pipes[name] then
            -- The sandbox close wrapper raises on failure before ownership is released.
            pipes[name]:close()
            pipes[name] = nil
        end
    end
    local function read(size)
        if size == 0 then
            return ""
        end
        local count, data = pipes.parent_out:read(bytes(size + 1), size, {block = true, timeout = 10000})
        assert(count == size, "analyzer did not produce a complete response")
        return data:str()
    end
    local service = {}
    local function send(payload, split)
        local encoded = frame(payload)
        if split then
            for index = 1, #encoded do
                assert(pipes.parent_in:write(encoded:sub(index, index), {block = true, timeout = 10000}) == 1)
            end
        else
            assert(pipes.parent_in:write(encoded, {block = true, timeout = 10000}) == #encoded)
        end
    end
    function service.request(payload, split)
        send(payload, split)
        local header = read(4)
        local a, b, c, d = header:byte(1, 4)
        local size = a + b * 256 + c * 65536 + d * 16777216
        assert(size <= 16 * 1024 * 1024, "unbounded response frame")
        return decode(read(size))
    end
    function service.wait()
        local done, code = child:wait(10000)
        reaped = done == 1
        assert(done == 1 and code == 0, string.format("analyzer did not exit successfully: wait=%s, status=%s", tostring(done), tostring(code)))
        assert(io.readfile(stderr, {encoding = "binary"}) == "", "unexpected analyzer stderr")
    end
    try {
        function ()
            -- Child stdio blocks; parent endpoints use Xmake's timed scheduler waits.
            pipes.child_in, pipes.parent_in = pipe.openpair("BA")
            pipes.parent_out, pipes.child_out = pipe.openpair("AB")
            child = process.openv(program, {}, {stdin = pipes.child_in, stdout = pipes.child_out, stderr = stderr})
            close_pipe("child_in")
            close_pipe("child_out")
            -- Register before requests: Xmake's POSIX poller can reap unwatched children.
            -- A timed wait retains the registration and any subsequent exit status.
            local done, code = child:wait(1)
            reaped = done == 1
            assert(done == 0, string.format("analyzer exited before requests: wait=%s, status=%s", tostring(done), tostring(code)))
            run(service)
        end,
        finally {function (ok, error)
            completed, failure = ok, error
            close_pipe("child_in")
            close_pipe("child_out")
            close_pipe("parent_in")
            close_pipe("parent_out")
            if child then
                if not reaped then
                    child:kill()
                    child:wait(10000)
                end
                child:close()
            end
            if ok then
                os.tryrm(stderr)
            end
        end}
    }
    -- Xmake's try runs finally but does not rethrow an uncaught body failure.
    if not completed then
        raise("%s: %s\nanalyzer stderr path: %s", name, tostring(failure), stderr)
    end
end

local function interactive(program)
    with_service(program, "serial session", function (service)
        local library = "export fn answer() -> i32 { return 42; }"
        local caller = "import lib using answer; fn f() -> i32 { let v = answer(); return v; }"
        assert(service.request(update("lib", 1, library), true).tag == 1)
        assert(service.request(update("app", 4, caller)).tag == 1)
        local selection = string.char(3) .. integer(2, 4) .. text("lib") .. text("lib") .. text("app") .. text("app")
        assert(service.request(selection).tag == 1)
        local call = assert(caller:find("answer()", 1, true)) - 1
        local definition = service.request(query(6, "app", call))
        assert(definition.tag == 4 and definition.location.document == "lib" and definition.location.version == 1)
        assert(#definition.document_versions == 2)
        -- Complete malformed operations must be rejected before updating session state.
        assert(service.request(update("lib", 2, "invalid") .. "extra").code == "request")
        assert(service.request(query(6, "app", call)).location.version == 1)
        local refs = service.request(query(7, "app", call)).locations
        assert(#refs == 2 and refs[1].document == "app" and refs[2].document == "lib")
        local v = assert(caller:find("return v", 1, true)) + 6
        assert(service.request(query(5, "app", v)).hover.type_text == "i32")
        assert(service.request(query(5, "app", #caller)).hover == nil)
        assert(service.request(update("app", 5, "fn bad() -> i32 { return unknown; }")).tag == 1)
        local failed = service.request(string.char(4))
        assert(not failed.published and #failed.diagnostics > 0)
        local found = false
        for _, diagnostic in ipairs(failed.diagnostics) do
            if diagnostic.primary and diagnostic.primary.location.document == "app" then
                assert(diagnostic.primary.location.version == 5)
                found = true
            end
        end
        assert(found, "diagnostics must carry source versions")
        local static = 'const { print("你\\0好\\r\\n"); } fn f() {}'
        assert(service.request(update("app", 6, static)).tag == 1)
        local checked = service.request(string.char(4))
        assert(checked.published and #checked.output == 1 and checked.output[1].bytes == "你\0好\r\n")
        assert(service.request(string.char(8)).tag == 1)
        -- Stop exits while the caller still holds its input pipe open.
        service.wait()
    end)
end

function main(target)
    local program = path.absolute(target:dep("carven-analyzer"):targetfile(), os.projectdir())
    interactive(program)
    local cases = {
        {name = "frame_boundary_eof", input = frame(update("document", 1, "fn f() {}")),
            code = 0, output = frame(string.char(1) .. integer(0, 4))},
        {name = "partial_header", input = "\1\0", code = 2, output = "",
            error = "truncated frame header"},
        {name = "partial_payload", input = integer(2, 4) .. "\4", code = 2, output = "",
            error = "truncated frame payload"},
        {name = "oversized_frame", input = integer(16 * 1024 * 1024 + 1, 4),
            code = 2, output = "", error = "frame limit"},
    }
    for _, case in ipairs(cases) do
        local prefix = os.tmpfile()
        io.writefile(prefix .. ".in", case.input, {encoding = "binary"})
        local code = os.execv(program, {}, {try = true, timeout = 10000, stdin = prefix .. ".in", stdout = prefix .. ".out", stderr = prefix .. ".err"})
        assert(code == case.code, case.name .. ": wrong exit code")
        assert(io.readfile(prefix .. ".out", {encoding = "binary"}) == case.output, case.name .. ": unexpected stdout")
        local error = io.readfile(prefix .. ".err", {encoding = "binary"})
        if case.error then
            assert(error:find(case.error, 1, true), case.name .. ": wrong error")
        else
            assert(error == "", case.name .. ": unexpected stderr")
        end
        os.rm(prefix .. ".in")
        os.rm(prefix .. ".out")
        os.rm(prefix .. ".err")
    end
    return true
end
