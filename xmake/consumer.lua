function configuration(compiler, crafts)
    local project = import("core.project.project")
    local package = project.required_package("carven")
    local prefix = assert(package and package:installdir(),
        "build the root project before running consumer tests or benchmarks")
    return string.format([[package("carven")
    set_kind("toolchain")
    set_installdir(%q)
    on_fetch(function () return {} end)
package_end()
add_requires("carven", {system = false})
set_values("carven.program", %q)
set_values("carven.craftsdir", %q)]], prefix, compiler, crafts), path.join(prefix, "rules")
end
