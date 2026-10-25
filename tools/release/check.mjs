import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { cp, mkdir, mkdtemp, realpath, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { basename, delimiter, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const repository = fileURLToPath(new URL('../../', import.meta.url));
const windows = process.platform === 'win32';
const suffix = windows ? '.exe' : '';
const archive = join(
    repository,
    `build/packages/carven-${process.platform}-${process.arch}.tar.gz`,
);
const root = await realpath(await mkdtemp(join(tmpdir(), 'carven-install ')));
const prefix = join(repository, 'build/installed');
const bin = join(prefix, 'bin');
const tar = windows ? join(process.env.SystemRoot, 'System32/tar.exe') : 'tar';
const systemPath = windows
    ? [
          join(process.env.SystemRoot, 'System32'),
          join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0'),
      ].join(delimiter)
    : '/usr/bin:/bin:/usr/sbin:/sbin';
const environment = {
    ...process.env,
    XDG_CACHE_HOME: join(root, 'cache'),
    XDG_CONFIG_HOME: join(root, 'config'),
    XMAKE_GLOBALDIR: join(root, 'xmake'),
    PATH: bin + delimiter + (process.env.PATH ?? ''),
};
const runtimeEnvironment = { ...environment, PATH: bin + delimiter + systemPath };
let passed = false;

async function run(program, args, cwd, env = environment) {
    const child = spawn(program, args, { cwd, env, stdio: ['ignore', 'pipe', 'pipe'] });
    let output = '';
    let errors = '';
    child.stdout.on('data', (data) => (output += data));
    child.stderr.on('data', (data) => (errors += data));
    const timeout = setTimeout(() => child.kill(), 300000);

    try {
        const code = await new Promise((resolve, reject) => {
            child.on('error', reject);
            child.on('close', resolve);
        });
        assert.equal(code, 0, `${program} ${args.join(' ')}\n${output}${errors}`);
        return output;
    } finally {
        clearTimeout(timeout);
    }
}

try {
    await rm(prefix, { recursive: true, force: true });
    await mkdir(prefix, { recursive: true });
    await cp(archive, join(prefix, basename(archive)));
    // Windows tar narrows path arguments; use an ASCII filename in its working directory.
    await run(tar, ['-xzf', basename(archive)], prefix, runtimeEnvironment);
    await rm(join(prefix, basename(archive)));

    const source = 'println(42);\n';
    for (const buildSystem of ['carven', 'xmake', 'cmake']) {
        const project = join(root, buildSystem);
        await mkdir(project);
        await writeFile(join(project, 'main.cv'), source);
        if (buildSystem === 'carven') {
            await writeFile(
                join(project, 'build.cv'),
                `import std::build using project;
project({targets: [{name: "app", kind: .Executable, sources: ["main.cv"], ...}], ...})?;
`,
            );
            await run(
                'carven' + suffix,
                ['format', 'check', 'main.cv'],
                project,
                runtimeEnvironment,
            );
            await run('carven' + suffix, ['build'], project);
        } else if (buildSystem === 'xmake') {
            await writeFile(
                join(project, 'xmake.lua'),
                `includes(${JSON.stringify(join(prefix, 'share/xmake/carven'))})
set_toolchains(${JSON.stringify(windows ? 'mingw[clang]' : 'llvm')})
${windows ? 'set_runtimes("c++_shared")' : ''}
target("app")
    set_kind("binary")
    set_targetdir("build")
    add_rules("carven")
    add_files("main.cv")
`,
            );
            await run('xmake', ['build', '-y'], project);
        } else {
            await writeFile(
                join(project, 'CMakeLists.txt'),
                `cmake_minimum_required(VERSION 3.21)
project(installed LANGUAGES CXX)
set(Carven_DIR "${join(prefix, 'share/cmake/Carven').replaceAll('\\', '/')}")
find_package(Carven CONFIG REQUIRED)
add_executable(app)
carven_target_sources(app main.cv)
`,
            );
            await run(
                'cmake',
                [
                    '-S',
                    '.',
                    '-B',
                    'build',
                    '-G',
                    'Ninja',
                    '-DCMAKE_BUILD_TYPE=Release',
                    '-DCMAKE_CXX_COMPILER=clang++',
                    '-DCMAKE_CXX_FLAGS=-stdlib=libc++',
                ],
                project,
            );
            await run('cmake', ['--build', 'build'], project);
        }
        assert.equal((await run(join(project, 'build/app' + suffix), [], project)).trim(), '42');
    }

    const embedding = join(root, 'embedding');
    await mkdir(embedding);
    await writeFile(
        join(embedding, 'main.cpp'),
        `#include <carven/hosting/api.hpp>
#include <cstdint>
#include <iostream>
#include <variant>

auto main() -> int {
    auto program = HostProgram::load("app", "fn answer() -> i32 => 42;");
    if (!program) { std::cerr << program.error().message; return 1; }
    auto function = program->function("answer");
    if (!function) { std::cerr << function.error().message; return 1; }
    auto result = function->call({}, host_call_options());
    if (!result) { std::cerr << result.error().message; return 1; }
    const auto* value = std::get_if<HostValue>(&*result);
    const auto* integer = value ? std::get_if<std::int32_t>(value) : nullptr;
    if (!integer) return 1;
    std::cout << *integer;
}
`,
    );
    await run(
        'clang++',
        [
            '-std=c++23',
            '-stdlib=libc++',
            '-I' + join(prefix, 'include'),
            'main.cpp',
            join(prefix, 'lib', windows ? 'carven-hosting.lib' : 'libcarven-hosting.a'),
            ...(process.platform === 'linux' ? ['-lc++abi'] : []),
            '-o',
            'embedding' + suffix,
        ],
        embedding,
    );
    assert.equal((await run(join(embedding, 'embedding' + suffix), [], embedding)).trim(), '42');

    passed = true;
    console.log('Installed package: Carven/Xmake/CMake builds, formatter and C++23 SDK passed');
} finally {
    if (passed) {
        await rm(root, { recursive: true, force: true });
    } else {
        console.error('Package check retained:', root, prefix);
    }
}
