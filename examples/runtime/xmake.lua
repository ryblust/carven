target("carven-example-runtime-code")
    set_kind("static")
    set_languages("c++20")
    add_rules("carven")
    add_files("preview.cv", "preview_provider.cpp")
target_end()

target("carven-example-runtime")
    set_kind("binary")
    set_languages("c++23")
    add_deps("carven-hosting", "carven-example-runtime-code")
    add_files("main.cpp")
    on_load(function (target)
        target:rule_enable("c++.build.modules", false)
        target:rule_enable("c++.build.modules.scanner", false)
        target:rule_enable("c++.build.modules.builder", false)
    end)
    add_tests("preview", {
        group = "runtime", run_timeout = 30000, plain = true,
        pass_outputs = {"query: draft\nquery: revised draft\nrevision 2: updated: revised response\nview: updated: revised response\nrevision 1: preview: original response\nview after older reply: updated: revised response\nquery: closing draft\ncancelled pending query\neditor closed\n",
            "query: draft\r\nquery: revised draft\r\nrevision 2: updated: revised response\r\nview: updated: revised response\r\nrevision 1: preview: original response\r\nview after older reply: updated: revised response\r\nquery: closing draft\r\ncancelled pending query\r\neditor closed\r\n"},
    })
target_end()
