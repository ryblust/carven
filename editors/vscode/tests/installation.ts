import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { mkdir, mkdtemp, readFile, realpath, rm, stat, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { delimiter, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Client, hover } from './lsp/client.ts';

const repository = fileURLToPath(new URL('../../../', import.meta.url));
const windows = process.platform === 'win32';
const suffix = windows ? '.exe' : '';
const artifacts = join(repository, 'build/packages');
const platform = `${process.platform}-${process.arch}`;
const archive = join(artifacts, `carven-${platform}.tar.gz`);
const root = await realpath(await mkdtemp(join(tmpdir(), 'carven-install 中文 ')));
const prefix = join(root, 'toolchain');
const bin = join(prefix, 'bin');
const tar = windows ? join(process.env.SystemRoot!, 'System32/tar.exe') : 'tar';
const systemPath = windows
    ? [
          join(process.env.SystemRoot!, 'System32'),
          join(process.env.SystemRoot!, 'System32/WindowsPowerShell/v1.0'),
      ].join(delimiter)
    : '/usr/bin:/bin:/usr/sbin:/sbin';
const environment = {
    ...process.env,
    XDG_CACHE_HOME: join(root, 'cache'),
    XDG_CONFIG_HOME: join(root, 'config'),
    XMAKE_GLOBALDIR: join(root, 'xmake'),
    PATH: bin + delimiter + (process.env.PATH ?? ''),
};
// Compiler queries and LSP startup use only the package and system utilities.
const runtimeEnvironment = { ...environment, PATH: bin + delimiter + systemPath };
let passed = false;

async function run(
    program: string,
    args: string[],
    cwd: string,
    env: NodeJS.ProcessEnv = environment,
): Promise<string> {
    const child = spawn(program, args, { cwd, env, stdio: ['ignore', 'pipe', 'pipe'] });
    let output = '';
    let errors = '';
    child.stdout.on('data', (data) => (output += data));
    child.stderr.on('data', (data) => (errors += data));
    const timeout = setTimeout(() => child.kill(), 300000);

    try {
        const code = await new Promise<number | null>((resolve, reject) => {
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
    await mkdir(prefix);
    await run(tar, ['-xzf', archive, '-C', prefix], root, runtimeEnvironment);
    const version = (await readFile(join(prefix, '.carven-toolchain'), 'utf8')).trim();
    assert.equal(
        (await run('carven' + suffix, ['--version'], root, runtimeEnvironment)).trim(),
        'carven v' + version,
    );

    const project = join(root, 'project');
    await mkdir(project);
    const source = join(project, 'main.cv');
    await writeFile(source, 'fn sample() { let value = ; }');
    await writeFile(
        join(project, 'build.cv'),
        `import std::build using project;
project({targets: [{name: "installed", kind: .Executable, sources: ["main.cv"], ...}], ...})?;
`,
    );
    const model = JSON.parse(await run('carven' + suffix, ['query'], project, runtimeEnvironment));
    assert.equal(resolve(model.project.root), project);
    assert.deepEqual(
        model.project.targets.map((target: { name: string; sources: string[] }) => ({
            name: target.name,
            sources: target.sources.map((source) => resolve(source)),
        })),
        [{ name: 'installed', sources: [source] }],
    );
    assert(!existsSync(join(project, 'build')));
    await writeFile(
        source,
        `import std::json using JSONValue;
let document = JSONValue::parse(r#"{"answer":42}"#)?;
println(document.get("answer")?.as_i64()?);
`,
    );
    await run('carven' + suffix, ['build'], project);
    assert.equal(
        (await run(join(project, 'build', 'installed' + suffix), [], project)).trim(),
        '42',
    );

    await writeFile(source, (await readFile(source, 'utf8')).replace('42', '43'));
    await run('carven' + suffix, ['run', 'build.cv'], project);
    assert.equal(
        (await run(join(project, 'build', 'installed' + suffix), [], project)).trim(),
        '43',
    );

    for (const buildSystem of ['xmake', 'cmake']) {
        const consumer = join(root, buildSystem + '-project');
        await mkdir(consumer);
        await writeFile(
            join(consumer, 'main.cv'),
            'import .answer using answer;\nprintln(answer()?);\n',
        );
        const answer = join(consumer, 'answer.cv');
        await writeFile(
            answer,
            `import values::api using value;

import std::json using { JSONError, JSONValue, JSONValueError }

fn answer() -> i64 throw JSONError + JSONValueError {
    let document = JSONValue::parse(r#"{"answer":41}"#)?;
    return document.get("answer")?.as_i64()? + value();
}
`,
        );

        const craft = join(consumer, 'crafts/values');
        await mkdir(craft, { recursive: true });
        await writeFile(
            join(craft, 'api.cv'),
            `import <values/value.hpp>;
private import(cpp) fn native_value() -> i64 = ::native_value;

export fn value() -> i64 => native_value();
`,
        );
        await writeFile(
            join(craft, 'value.hpp'),
            '#include <cstdint>\nstd::int64_t native_value() noexcept;\n',
        );
        for (const [name, value] of [
            ['first', 1],
            ['second', 2],
        ] as const) {
            await writeFile(
                join(craft, name + '.cpp'),
                `#include "value.hpp"\nstd::int64_t native_value() noexcept { return ${value}; }\n`,
            );
        }

        const selection = join(craft, 'selection.cv');
        const declaration = join(craft, 'build.cv');
        await writeFile(selection, 'const fn implementation() -> String => "first.cpp";\n');
        await writeFile(
            declaration,
            `import .selection using implementation;
import std::build using project;

project({
    targets: [{
        name: "value", kind: .SourceLibrary,
        sources: ["api.cv", implementation()], headers: ["value.hpp"], ...
    }],
    ...
})?;
`,
        );
        const query = JSON.parse(
            await run('carven' + suffix, ['query'], craft, runtimeEnvironment),
        );
        const scripts = query.scripts.map((path: string) => resolve(path));
        assert(scripts.includes(declaration) && scripts.includes(selection));
        assert(!scripts.includes(answer));

        if (buildSystem === 'xmake') {
            await writeFile(
                join(consumer, 'xmake.lua'),
                `includes(${JSON.stringify(join(prefix, 'share/xmake/carven'))})
set_toolchains(${JSON.stringify(windows ? 'mingw[clang]' : 'llvm')})
${windows ? 'set_runtimes("c++_shared")' : ''}
target("installed")
    set_kind("binary")
    set_targetdir("build/bin")
    add_rules("carven")
    add_files("main.cv")
`,
            );
        } else {
            await writeFile(
                join(consumer, 'CMakeLists.txt'),
                `cmake_minimum_required(VERSION 3.21)
project(installed LANGUAGES CXX)
set(Carven_DIR "${join(prefix, 'share/cmake/Carven').replaceAll('\\', '/')}")
find_package(Carven CONFIG REQUIRED)

add_executable(installed)
set_target_properties(installed PROPERTIES RUNTIME_OUTPUT_DIRECTORY "\${CMAKE_BINARY_DIR}/bin")
carven_target_sources(installed main.cv)
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
                consumer,
            );
        }

        const buildArguments = buildSystem === 'xmake' ? ['build', '-y'] : ['--build', 'build'];
        const executable = join(consumer, 'build/bin', 'installed' + suffix);
        await run(buildSystem, buildArguments, consumer);
        assert.equal((await run(executable, [], consumer)).trim(), '42');

        const built = await stat(executable);
        await run(buildSystem, buildArguments, consumer);
        assert.equal((await stat(executable)).mtimeMs, built.mtimeMs);

        await writeFile(answer, (await readFile(answer, 'utf8')).replace('41', '42'));
        await run(buildSystem, buildArguments, consumer);
        assert.equal((await run(executable, [], consumer)).trim(), '43');

        await writeFile(selection, (await readFile(selection, 'utf8')).replace('first', 'second'));
        await run(buildSystem, buildArguments, consumer);
        assert.equal((await run(executable, [], consumer)).trim(), '44');
    }

    const embedding = join(root, 'embedding');
    await mkdir(embedding);
    await writeFile(
        join(embedding, 'native.hpp'),
        `#pragma once
#include <cstdint>

auto native_plus(std::int32_t value) noexcept -> std::int32_t;
`,
    );
    const hostedSource = `import "native.hpp";
import(cpp) async fn query() -> i32;

async fn run() -> i32 {
    print("first;");
    let value = await query();
    return ::native_plus(value) as i32;
}
`;
    await writeFile(join(embedding, 'installed.cv'), hostedSource);
    await run(
        'carven' + suffix,
        ['compile', 'installed.cv', '-o', 'generated'],
        embedding,
        runtimeEnvironment,
    );
    await writeFile(
        join(embedding, 'main.cpp'),
        String.raw`#include "native.hpp"

#include <carven/hosting/api.hpp>
#include <carven/hosting/entry.hpp>
#include <carven/hosting/method.hpp>
#include <carven/hosting/operation.hpp>
#include <carven/hosting/request.hpp>
#include <carven/hosting/service.hpp>
#include <carven/hosting/standard.hpp>
#include <carven/providers/installed.hpp>

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace query = carven::providers::installed::cv_escaped_functions::query;
using Query = std::remove_cvref_t<decltype(query::service)>;
using Requests = std::vector<HostRequest<Query::Response>>;

auto native_plus(std::int32_t value) noexcept -> std::int32_t {
    return value + 1;
}

auto start_query(void* context, HostRequest<Query::Response> request) noexcept -> void {
    static_cast<Requests*>(context)->push_back(std::move(request));
}

auto pump(HostSession& session) noexcept -> HostSessionStatus {
    auto state = session.poll(64);
    while (state == HostSessionStatus::Runnable) state = session.poll(64);
    return state;
}

auto completed(const HostTask& task, std::int32_t expected) noexcept -> bool {
    const auto* result = task.result();
    if (result == nullptr || !*result) {
        if (result != nullptr) std::cerr << result->error().message << '\n';
        return false;
    }
    const auto* value = std::get_if<HostValue>(&**result);
    const auto* integer = value ? std::get_if<std::int32_t>(value) : nullptr;
    return integer != nullptr && *integer == expected;
}

auto main() -> int {
    auto output = std::string();
    auto requests = Requests();
    auto services = host_standard_services();
    services.push_back(host_cpp_operation<native_plus>("native_plus"));
    services.push_back(host_async_service<start_query, nullptr>(query::service, &requests));
    auto options = host_session_options();
    options.services = services;
    options.calls.output = {
        .context = &output,
        .write = [](void* context, bool, std::string_view text) noexcept {
            static_cast<std::string*>(context)->append(text);
        }
    };
    auto session = HostSession::load("installed", R"(${hostedSource})", options);
    if (!session) {
        std::cerr << session.error().message << '\n';
        return 1;
    }
    const auto first = session->submit("run", {});
    if (!first || pump(*session) != HostSessionStatus::Waiting
        || first->result() != nullptr || requests.size() != 1) return 1;

    const auto replaced = session->reload(R"(
import "native.hpp";
import(cpp) async fn query() -> i32;

async fn run() -> i32 {
    print("second;");
    let value = await query();
    return (::native_plus(value) as i32) + 1;
}
)");
    if (!replaced) {
        std::cerr << replaced.error().message << '\n';
        return 1;
    }
    const auto second = session->submit("run", {});
    if (!second || pump(*session) != HostSessionStatus::Waiting
        || second->result() != nullptr || requests.size() != 2) return 1;

    if (!requests[1].complete(40) || pump(*session) != HostSessionStatus::Waiting
        || !completed(*second, 42) || first->result() != nullptr) return 1;
    if (!requests[0].complete(40) || pump(*session) != HostSessionStatus::Idle
        || !completed(*first, 41)) return 1;
    std::cout << output;
}
`,
    );
    await writeFile(
        join(embedding, 'xmake.lua'),
        `set_languages("c++23")
set_toolchains(${JSON.stringify(windows ? 'mingw[clang]' : 'llvm')})
set_runtimes("c++_shared")
target("embedding")
    set_kind("binary")
    add_files("main.cpp")
    add_includedirs(${JSON.stringify(join(prefix, 'include'))})
    add_includedirs(".", "generated")
    add_linkdirs(${JSON.stringify(join(prefix, 'lib'))})
    add_links("carven-hosting")
    if is_plat("linux") then add_syslinks("c++abi") end
`,
    );
    await run('xmake', ['build', '-y'], embedding);
    assert.equal((await run('xmake', ['run', 'embedding'], embedding)).trim(), 'first;second;');

    process.env.CARVEN_LSP = join(prefix, 'bin', 'carven-lsp' + suffix);
    const client = new Client([], runtimeEnvironment, root);
    try {
        await client.initialize();
        const uri = 'untitled:Installed';
        const text = 'fn sample() { let value: i32 = 42; println(value); }';
        await client.send('textDocument/didOpen', {
            textDocument: { uri, languageId: 'carven', version: 1, text },
        });
        assert.deepEqual(await client.diagnostics(uri, 1), []);
        await hover(client, uri, text, 'value:', 2, 'i32');
        await client.finish();
    } finally {
        await client.close();
    }

    if (process.env.CARVEN_TEST_VSCODE === '1') {
        const extension = JSON.parse(
            await readFile(join(repository, 'editors/vscode/package.json'), 'utf8'),
        );
        const editorEnvironment = {
            ...environment,
            CARVEN_LSP: join(bin, 'carven-lsp' + suffix),
            CARVEN_XMAKE: 'xmake',
            CARVEN_VSIX: join(artifacts, `carven-${extension.version}.vsix`),
        };
        await run(
            process.execPath,
            [join(repository, 'editors/vscode/build.mjs'), '--tests'],
            repository,
            editorEnvironment,
        );
        process.stdout.write(
            await run(
                process.execPath,
                [join(repository, 'editors/vscode/tests/run-integration.ts')],
                repository,
                editorEnvironment,
            ),
        );
    }

    passed = true;
    console.log(
        'Packaged toolchain: native build/query, Xmake/CMake incremental builds, C++23 hosting reload and LSP passed',
    );
} finally {
    if (passed) {
        await rm(root, { recursive: true, force: true });
    } else {
        console.error('Packaged toolchain artifacts retained:', root);
    }
}
