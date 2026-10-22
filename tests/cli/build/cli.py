"""Exercise source commands and composable builds through a relocated toolchain."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    compiler, crafts = (Path(value).resolve() for value in sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="carven-cli-") as temporary:
        root = Path(temporary)
        installed = root / "installed"
        real = installed / "bin/carven"
        real.parent.mkdir(parents=True)
        shutil.copy2(compiler, real)
        shutil.copytree(crafts, installed / "crafts")
        (installed / "crafts/carven/closure_fixture.cv").write_text(
            'private struct Stamp { tag: i32 }\n'
            'private fn offset(tag: i32) -> i32 => tag + 23;\n'
            'export fn make<T>(tag: i32) {\n'
            '    let stamp = Stamp { tag: tag };\n'
            '    return [stamp](&&value: T) -> (i32, T) => (offset(stamp.tag), &&value);\n'
            '}\n'
        )
        (root / "bin").mkdir()
        command = root / "bin/carven"
        command.symlink_to(real)
        project = root / "plain project"
        project.mkdir()
        payload = ["--help", "你好🚀 space", "", 'a"b\\']
        closure_import = 'import carven::closure_fixture using make;\n'
        integer_closure_checks = (
            'let number = make<i32>(19);\n'
            'let (number_tag, number_value) = number(&&7);\n'
            'assert(number_tag == 42 && number_value == 7);\n'
        )
        (project / "main.cv").write_text(
            'import std::process using arguments;\n'
            + closure_import
            + integer_closure_checks
            + 'for argument in arguments()? { println(argument); }\n'
            'println("toolchain available");\n'
        )
        environment = {**os.environ, "PATH": f"{root / 'bin'}{os.pathsep}{os.environ['PATH']}"}
        result = subprocess.run(["carven", "build", "main.cv"], cwd=project,
                                env=environment, check=True, timeout=60,
                                stdout=subprocess.PIPE, text=True)
        assert "Built build/main\n" in result.stdout
        result = subprocess.run(["carven", "run", "main.cv", "--", *payload], cwd=project,
                                env=environment, check=True, timeout=60,
                                stdout=subprocess.PIPE, text=True)
        assert result.stdout.endswith("\n".join([*payload, "toolchain available", ""]))
        (project / "build.cv").write_text(
            'import std::process using { arguments, run }\n'
            'let supplied = arguments()?;\n'
            f'run({json.dumps(sys.executable)}, supplied.slice(3, supplied.length()))?;\n'
        )
        argument_check = f"import sys; assert sys.argv[1:] == {payload!r}; print('arguments preserved')"
        result = subprocess.run(["carven", "run", "arguments", "--", "-c", argument_check, *payload],
                                cwd=project, env=environment, check=True, timeout=15,
                                stdout=subprocess.PIPE, text=True)
        assert result.stdout == "arguments preserved\n"

        native = project / "native"
        native.mkdir()
        (native / "values.hpp").write_text("int native_value() noexcept;\n")
        (native / "values.cpp").write_text(
            '#include <values.hpp>\n'
            'int named_value() noexcept;\nint file_value() noexcept;\n'
            'int native_value() noexcept { return BUILD_VALUE + named_value() + file_value(); }\n'
        )
        cxx = os.environ.get("CXX", "clang++")
        for name in ["named", "file"]:
            source = native / f"{name}.cpp"
            source.write_text(f"int {name}_value() noexcept {{ return 1; }}\n")
            obj = native / f"{name}.o"
            subprocess.run([cxx, "-c", str(source), "-o", str(obj)], check=True, timeout=15)
            subprocess.run(["ar", "rcs", str(native / f"lib{name}.a"), str(obj)],
                           check=True, timeout=15)
        (project / "generate.py").write_text(
            'from pathlib import Path\n'
            'Path("generated.cv").write_text("fn generated() -> i32 => 0;\\n")\n'
        )
        (project / "support.cv").write_text(
            'import <values.hpp>;\n'
            'private import(cpp) fn native_value() -> i32 = ::native_value;\n'
            'export fn value() -> i32 => native_value();\n'
        )
        (project / "main.cv").write_text(
            'import .support using value;\n'
            'import .generated using generated;\n'
            + closure_import
            + integer_closure_checks
            + 'let owner = make<String>(20);\n'
            'let payload: String = "kept";\n'
            'let (text_tag, text_value) = owner(&&payload);\n'
            'assert(text_tag == 43 && text_value.as_str() == "kept");\n'
            'println(value() + generated());\n'
        )
        plan = '''{
    output_directory: ".",
    targets: [
        {
            name: "support",
            kind: .StaticLibrary,
            sources: ["support.cv"],
            native_sources: ["native/values.cpp"],
            native: {
                include_dirs: ["native"],
                defines: ["BUILD_VALUE=40"],
                compile_flags: ["-Wall"],
                library_dirs: ["native"],
                libraries: [LinkLibrary::Name("named"), LinkLibrary::File("native/libfile.a")],
                ...
            },
            ...
        },
        {
            name: "application",
            kind: .Executable,
            sources: ["main.cv", "generated.cv"],
            dependencies: ["support"],
            output: "products/bin/application",
            native: { include_dirs: ["native"], link_flags: ["-Wl,-v"], ... },
            ...
        },
    ],
    ...
}'''
        preparation = (
            'import std::build using { build, BuildError, LinkLibrary }\n'
            'import std::process using run;\n'
            f'assert(run({json.dumps(sys.executable)}, ["generate.py"])? == 0);\n'
        )
        rejected_cycle = '''
assert(try {
    build({
        targets: [
            { name: "a", kind: .StaticLibrary, dependencies: ["b"], ... },
            { name: "b", kind: .StaticLibrary, dependencies: ["a"], ... },
        ],
        ...
    })?;
    false
} catch {
    BuildError(_) => true,
});
'''
        script = project / "build.cv"
        script.write_text(preparation + rejected_cycle + f'let outputs = build({plan})?;\n'
                          'assert(outputs.length() == 2);\nprintln("build completed");\n')
        result = subprocess.run(["carven", "build"], cwd=project,
                                env=environment, check=True, timeout=60,
                                stdout=subprocess.PIPE, text=True)
        assert result.stdout.endswith("build completed\n")
        assert "42\n" not in result.stdout

        script.write_text(preparation + f'build({plan})?;\n')
        result = subprocess.run(["carven", "run"], cwd=project, env=environment,
                                check=True, timeout=60, stdout=subprocess.PIPE, text=True)
        assert result.stdout.endswith("42\n")

        with script.open("a") as source:
            source.write('println("script completed");\n')
        script.rename(project / "foo.cv")
        result = subprocess.run(["carven", "interpret", "foo.cv"], cwd=project,
                                env=environment, check=True, timeout=60,
                                stdout=subprocess.PIPE, text=True)
        assert result.stdout.endswith("script completed\n")
        assert "42\n" not in result.stdout


if __name__ == "__main__":
    main()
