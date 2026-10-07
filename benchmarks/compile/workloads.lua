function workloads()
    local groups = {}
    local cases = {}
    table.insert(groups, {title = "Functions and call chains", cases = cases})
    for _, count in ipairs({16, 64, 256}) do
        local independent, chain, reversed = {}, {}, {}
        for index = 0, count - 1 do
            table.insert(independent, string.format("fn func%d(x: i32) -> i32 { return x; }", index))
            table.insert(chain, string.format("fn func%d(x: i32) -> i32 { return %s; }", index,
                index + 1 < count and string.format("func%d(x)", index + 1) or "x"))
        end
        for index = #chain, 1, -1 do
            table.insert(reversed, chain[index])
        end
        for _, form in ipairs({{"independent", "Independent functions", independent},
            {"caller_first", "Caller declared first", chain},
            {"callee_first", "Callee declared first", reversed}}) do
            table.insert(cases, {name = form[1] .. "_" .. count, label = form[2], size = count .. " functions",
                source = table.concat(form[3], "\n") .. "\nfn main() { let _ = func0(1); }\n"})
        end
    end
    do
        local count = 64
        local stops = {}
        for index = 0, count - 1 do
            table.insert(stops, string.format("fn stop%d() -> void { %s; }", index,
                index + 1 < count and string.format("stop%d()", index + 1) or "fail()"))
        end
        table.insert(cases, {name = "test_stop_chain_" .. count, label = "Test-stop call chain", size = count .. " functions",
            source = table.concat(stops, "\n") .. "\nfn main() { stop0(); }\n"})
    end
    cases = {}
    table.insert(groups, {title = "Shared dependencies", cases = cases})
    for _, depth in ipairs({8, 16, 24}) do
        local declarations = {"struct N0 { value: i32 }"}
        for index = 1, depth do
            table.insert(declarations, string.format("struct N%d { left: N%d, right: N%d }",
                index, index - 1, index - 1))
        end
        table.insert(declarations, string.format("fn inspect(value: N%d) {}", depth))
        table.insert(cases, {name = "shared_nominal_" .. depth, label = "Nominal fields", size = "depth " .. depth,
            source = table.concat(declarations, "\n")})
    end
    for _, depth in ipairs({4, 8, 12}) do
        local statements = {"let x0 = values[0];"}
        for index = 1, depth do
            table.insert(statements, string.format("let x%d = x%d + x%d;", index, index - 1, index - 1))
        end
        table.insert(cases, {name = "shared_native_query_" .. depth, label = "Native result queries", size = "depth " .. depth,
            source = "import <vector> using std::vector;\nfn probe(values: vector<i32>) { "
                .. table.concat(statements, " ") .. string.format(" return x%d; }\n", depth)})
    end
    cases = {}
    table.insert(groups, {title = "Pattern coverage", cases = cases})
    for _, count in ipairs({16, 64, 256}) do
        local members, arms = {}, {}
        for index = 0, count - 1 do
            table.insert(members, string.format("Case%d,", index))
            table.insert(arms, string.format(".Case%d => %d,", index, index))
        end
        table.insert(cases, {name = "wide_enum_" .. count, label = "Enum cases", size = count .. " cases",
            source = "enum Value { " .. table.concat(members, " ") .. " }\n"
                .. "fn probe(value: Value) -> i32 { return match value { "
                .. table.concat(arms, " ") .. " }; }\n"})
    end
    for _, width in ipairs({4, 8, 12}) do
        local types, arms = {}, {}
        for _ = 1, width do table.insert(types, "bool") end
        -- Each arm constrains one field; the other fields must stay unconstrained.
        for selected = 1, width do
            local fields = {}
            for index = 1, width do
                table.insert(fields, index == selected and "false" or "_")
            end
            table.insert(arms, ".Bits(" .. table.concat(fields, ", ") .. ") => " .. selected .. ",")
        end
        table.insert(arms, "_ => 0,")
        table.insert(cases, {name = "independent_payload_" .. width, label = "Independent payload fields",
            size = width .. " fields/arms",
            source = "enum Value { Bits(" .. table.concat(types, ", ") .. "), }\n"
                .. "fn probe(value: Value) -> i32 { return match value { "
                .. table.concat(arms, " ") .. " }; }\n"})
    end
    cases = {}
    table.insert(groups, {title = "Constants", cases = cases})
    do
        local count = 256
        for _, distinct in ipairs({false, true}) do
            local functions = {}
            for index = 0, count - 1 do
                table.insert(functions, string.format("fn func%d() -> i32 { return %d; }",
                    index, distinct and index or 1))
            end
            table.insert(cases, {name = (distinct and "distinct_constants_" or "repeated_constants_") .. count,
                label = distinct and "Distinct constants" or "Repeated constants", size = count .. " functions",
                source = table.concat(functions, "\n") .. "\nfn main() { let _ = func0(); }\n"})
        end
    end
    cases = {}
    table.insert(groups, {title = "Control flow and expressions", cases = cases})
    for _, count in ipairs({128, 4096}) do
        local operands = {}
        for _ = 1, count do table.insert(operands, "operand(value)") end
        table.insert(cases, {name = "operand_chain_" .. count, label = "Runtime operand chain", size = count .. " operands",
            source = "fn operand(value: i32) -> i32 => value;\n"
                .. "fn chain(value: i32) -> i32 { return " .. table.concat(operands, " + ") .. "; }\n"})
    end
    do
        local depth = 8
        local body = "x += 1;"
        for _ = 1, depth do
            body = "while flag { " .. body .. " break; }"
        end
        table.insert(cases, {name = "terminating_loops_" .. depth, label = "Loops with break", size = "depth " .. depth,
            source = "fn probe(flag: bool, &x: i32) { " .. body
                .. " }\nfn main() { var x = 0; probe(false, &x); }\n"})
    end
    do
        local count = 512
        local declarations, updates, reads = {}, {}, {}
        for index = 0, count - 1 do
            table.insert(declarations, string.format("    var local_%d: i32 = %d;", index, index))
            table.insert(updates, string.format("        local_%d += value;", index))
            table.insert(reads, string.format("    value += local_%d;", index))
        end
        table.insert(cases, {name = "ownership_locals_" .. count, label = "Mutable loop state",
            size = count .. " locals",
            source = "fn ownership_large(&value: i32) {\n" .. table.concat(declarations, "\n")
                .. "\n    for iteration in 0usize..8 {\n" .. table.concat(updates, "\n")
                .. "\n    }\n" .. table.concat(reads, "\n") .. "\n}\n"})
    end
    do
        local count = 128
        local parameters, reads, effects, calls = {}, {}, {}, {}
        for index = 1, count do
            table.insert(parameters, "_: i32")
            table.insert(reads, "value")
            table.insert(effects, index % 4 == 0 and "step(&value)" or "value")
            table.insert(calls, "operation(flag)?;")
        end
        for _, form in ipairs({{"wide_reads", "Argument reads", reads}, {"wide_effects", "Argument side effects", effects}}) do
            table.insert(cases, {name = form[1] .. "_" .. count, label = form[2], size = count .. " arguments",
                source = "fn collect(" .. table.concat(parameters, ", ") .. ") {}\n"
                    .. "fn step(&value: i32) -> i32 { value += 1; return value; }\n"
                    .. "fn main() { var value = 1; collect(" .. table.concat(form[3], ", ") .. "); }\n"})
        end
        table.insert(cases, {name = "fallible_roots_" .. count, label = "Error propagation", size = count .. " calls",
            source = "enum Error { Failed, }\n"
                .. "fn operation(flag: bool) -> i32 throw Error { if flag { return 1; } throw Error::Failed; }\n"
                .. "fn probe(flag: bool) throw Error { " .. table.concat(calls, " ") .. " }\n"
                .. "fn main() { try { probe(true)?; } catch { Error(_) => {}, } }\n"})
    end
    do
        local depth = 32
        local expression = "value"
        for _ = 1, depth do expression = "identity(" .. expression .. ")" end
        table.insert(cases, {name = "nested_expression_" .. depth, label = "Nested expressions", size = "depth " .. depth,
            source = "fn identity(value: i32) -> i32 => value;\n"
                .. "fn probe(value: i32) -> i32 { return " .. expression .. "; }\nfn main() { probe(1); }\n"})
    end
    return groups
end

-- Stable identifiers cover module batches and structured inputs in one selection.
function cases()
    local result = {}
    for _, count in ipairs({16, 128}) do
        table.insert(result, {id = "modules_" .. count, label = "Independent modules",
            size = count .. " modules", group = "Module batches", count = count})
    end
    for _, group in ipairs(workloads()) do
        for _, case in ipairs(group.cases) do
            case.id, case.group = case.name, group.title
            table.insert(result, case)
        end
    end
    table.insert(result, {id = "const_scalar_1000", label = "Constant scalar loop", size = "1000 iterations",
        group = "Constant execution", source = [[private const fn profile_bits() -> u32 {
    var result: u32 = 0;
    for index in 0usize..1000 {
        result = ((result << 1) | (result >> 31)) ^ (index as u32);
    }
    return result;
}
private const bits_result = profile_bits();
const test { check(bits_result == 118072920); }
]]})
    table.insert(result, {id = "const_simd_tables_32", label = "Constant SIMD tables", size = "32 x 32 lanes",
        group = "Constant execution", source = [[private const fn make_table(seed: u8) -> u8x32 {
    var entries: [u8; 32] = {};
    for index in 0usize..32 { entries[index] = (index as u8) ^ seed; }
    return u8x32::from_array(entries);
}
private const fn profile_tables() -> u8x32 {
    var result = u8x32::splat(0);
    for index in 0usize..32 { result = make_table(index as u8); }
    return result;
}
private const table_result = profile_tables();
const test { check(table_result.lane(0) == 31 && table_result.lane(31) == 0); }
]]})
    return result
end

function inputs(case)
    local sources = {}
    if case.count then
        for module = 0, case.count - 1 do
            sources[string.format("module_%03d.cv", module)] =
                string.format("export struct Value%03d { value: i32, }\n", module)
        end
    else
        sources[case.id .. ".cv"] = case.source
    end
    return sources
end
