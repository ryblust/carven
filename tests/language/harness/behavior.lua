local output_suffix = "containers\ntrue\ntuples\n42\ntail\ntrue\ntrue\ntrue\ntrue\ntrue\n"
    .. "constant\ntrue\ntrue\ntrue\n1234\n10065\n1241\ntrue\n"
    .. "contracts\n17\n13\n5\n10\n23\n20\n1\n12\n25\n2\n12\n9\n3\n12110\n129\n12384\n182\n"
    .. "owners\ntrue\ncomposition\ntrue\nwaiting\ntrue\n"
    .. "scheduling\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\n"
    .. "0\n9\n9\n4\n5\n6\n7\n7\n19\n11\n14\n21\n22\n2\n12\n0\nfalse\n58\n124433\n1384\n1284\n"

function main(target)
    return import("generated", {
        rootdir = path.join(os.projectdir(), "tests", "harness"),
    }).main(target, "language", output_suffix)
end
