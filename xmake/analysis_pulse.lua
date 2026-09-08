import("xmake.benchmark", {rootdir = os.projectdir()})

function main()
    local compiler, samples, warmups = benchmark.settings()
    local cases = {}
    for _, count in ipairs({64, 128, 256}) do
        local independent, chain, reversed = {}, {}, {}
        for index = 0, count - 1 do
            table.insert(independent, string.format("fn f%d(x: i32) -> i32 { return x; }", index))
            table.insert(chain, string.format("fn f%d(x: i32) -> i32 { return %s; }", index,
                index + 1 < count and string.format("f%d(x)", index + 1) or "x"))
        end
        for index = #chain, 1, -1 do
            table.insert(reversed, chain[index])
        end
        for _, form in ipairs({{"independent", independent}, {"caller_first", chain}, {"callee_first", reversed}}) do
            table.insert(cases, {name = form[1] .. "_" .. count,
                source = table.concat(form[2], "\n") .. "\nfn main() { let _ = f0(1); }\n"})
        end
    end
    for _, depth in ipairs({4, 8, 12, 16}) do
        local body = "x += 1;"
        for _ = 1, depth do
            body = "while flag { " .. body .. " break; }"
        end
        table.insert(cases, {name = "terminating_loops_" .. depth,
            source = "fn probe(flag: bool, &x: i32) { " .. body
                .. " }\nfn main() { var x = 0; probe(false, &x); }\n"})
    end
    print("End-to-end source-to-C++ timings, including process startup and generation.")
    benchmark.temporary(function (root)
        local errors = path.join(root, "stderr.txt")
        for _, case in ipairs(cases) do
            local filename = case.name .. ".cv"
            io.writefile(path.join(root, filename), case.source)
            local results = benchmark.measure(samples, warmups, function ()
                local status = os.execv(compiler, {"--stdout", filename}, {
                    curdir = root, stdout = os.nuldev(), stderr = errors, timeout = 60000,
                })
                if status ~= 0 then
                    raise("%s: %s", case.name, io.readfile(errors) or "compiler failed")
                end
            end)
            print("%24s: %9.2f ms", case.name, benchmark.median(results))
        end
    end)
end
