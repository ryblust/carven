local examples_dir = path.join(os.projectdir(), "examples")

local cases = {
    {name = "constant", dir = "constant", output = [[
Text
Carven build-0042
00,01,02,03
[Carven build-0042: 00,01,02,03]
Title bytes: 17
Runtime: 00,01,02
Array values
Selected: 7
Entries: 4
3
4
7
12
Runtime: 10 11 14 19
Static slices
Selected: 7
Entries: 4
3
4
7
12
Runtime: 10 11 14 19
Record tables
Selected: 11
import 10
const 11
struct 12
Runtime: const 21
]]},
    {name = "strings", dir = "strings", output = "Hello, 世界!\nGoodbye!\nHello, 世界!\nBytes: 14, ID: 002a\n"},
    {name = "hello-world", dir = "helloworld", output = "Hello World\n"},
    {name = "receipt", dir = "basics", output = [[
Notebook x 2
  Subtotal in cents: 500
  Discount in cents: 0
  Line total in cents: 500
Pencil x 3
  Subtotal in cents: 360
  Discount in cents: 36
  Line total in cents: 324
Subtotal in cents: 860
Savings in cents: 36
Total in cents: 824
]]},
    {name = "inventory", dir = "ownership",
        output = "Snapshot: 7\nDispatched: 9\nReplacement stock: 1\n"},
    {name = "shipping", dir = "modules", output = "Shipping in cents: 500\n"},
    {name = "failures", dir = "failures", output = [[
Basic failures
Seats remaining: 3
Not enough seats for: 6
Failure composition
Standard delivery
Quote in cents: 800
Alternate carrier
Quote in cents: 1200
Stock shortage
Requested: 6
Available: 5
Invalid quantity
Quantity must be between 1 and 100: 0
Oversized order
Quantity must be between 1 and 100: 101
Unknown destination
Unknown zone: 9
Unavailable destination
No carrier for zone: 3
Recovery lookup fails
Carrier lookup failed for zone: 4
Expression total: 800
Evaluation trace: 12
Expression total: -1
Evaluation trace: 1
Delivery affordable: 0
Delivery trace: 0
Delivery affordable: 1
Delivery trace: 2
Recovery
Primary setting
Port: 443
Backup setting
Port: 9000
Built-in default
Port: 8080
Invalid primary is not hidden
Bad digit at byte: 1
Invalid backup propagates
Port must be between 1 and 65535
Zero is not a port
Port must be between 1 and 65535
Callbacks
Basic policy
Accepted: 8
Rejected by basic policy
Amount must be positive: 0
Widened stored view
Accepted: 3
Captured policy
Accepted: 4
Rejected by captured policy
Requested: 8
Policy limit: 5
Invalid input through captured policy
Amount must be positive: -2
Failure payloads
Owned detail: request rejected
Taken detail: taken detail
Saved detail: borrowed detail
Source bytes after clear: 0
]]},
    {name = "native-parser", dir = "call_cpp", exceptions = true,
        output = "Port: 8080\nInvalid port\nInvalid port\nInvalid port\n"},
    {name = "cpp-host", dir = "cpp_host", output = "Price in cents:\n1080\n"},
}

local names = {}
for _, case in ipairs(cases) do
    local name = "carven-example-" .. case.name
    table.insert(names, name)
    target(name)
        set_default(false)
        set_kind("binary")
        set_languages("c++20")
        add_rules("@carven/carven")

        add_includedirs(path.join(examples_dir, case.dir))
        add_files(path.join(examples_dir, case.dir, "*.cv"))
        if case.name == "cpp-host" then
            add_files(path.join(examples_dir, case.dir, "main.cpp"))
        end
        if case.exceptions then
            set_exceptions("cxx")
            add_cxxflags("-frtti", {tools = "clang", force = true})
            add_cxxflags("/GR", "/U_HAS_EXCEPTIONS", "/D_HAS_EXCEPTIONS=1",
                {tools = {"cl", "clang_cl"}, force = true})
        end
        add_tests("output", {
            group = "examples",
            run_timeout = 30000,
            plain = true,
            pass_outputs = {case.output, (case.output:gsub("\n", "\r\n"))},
        })
    target_end()
end

target("examples")
    set_default(false)
    set_kind("phony")
    add_deps(table.unpack(names))
