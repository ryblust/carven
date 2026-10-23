target("carven-hosting")
    set_default(false)
    set_group("toolchain")
    set_kind("static")
    add_deps("carven-modules")
    add_files("src/**.cppm", {public = true, install = false})
    add_files("src/**.cpp")
    add_files(
        path.join(os.projectdir(), "crafts/carven/std/channel/native.cpp"),
        path.join(os.projectdir(), "crafts/carven/std/collections/native.cpp"),
        path.join(os.projectdir(), "crafts/carven/std/fs/native.cpp"),
        path.join(os.projectdir(), "crafts/carven/std/io/native.cpp"),
        path.join(os.projectdir(), "crafts/carven/std/process/native.cpp")
    )
    add_includedirs("include", {public = true})
    add_headerfiles("include/(carven/hosting/**.hpp)")
    add_headerfiles(path.join(os.projectdir(), "crafts/(carven/runtime/**.hpp)"))
target_end()
