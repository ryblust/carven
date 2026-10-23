import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { cp, mkdir } from 'node:fs/promises';
import { basename, join, resolve } from 'node:path';

async function bundleWindowsRuntime(prefix) {
    const root = process.env.LLVM_MINGW_ROOT;
    if (!root) throw new Error('Windows packaging requires LLVM_MINGW_ROOT');
    const architecture = { x64: 'x86_64', arm64: 'aarch64' }[process.arch];
    if (!architecture) throw new Error(`Unsupported Windows architecture: ${process.arch}`);
    const toolchain = join(root, `${architecture}-w64-mingw32`);
    const reader = join(root, 'bin/llvm-readobj.exe');
    const system = join(process.env.SystemRoot, 'System32');
    const directory = join(prefix, 'bin');
    const pending = ['carven', 'carven-format', 'carven-analyzer', 'carven-lsp'].map((name) =>
        join(directory, name + '.exe'),
    );
    const copied = new Set();
    while (pending.length) {
        const binary = pending.pop();
        const imports = execFileSync(reader, ['--coff-imports', binary], { encoding: 'utf8' });
        for (const [, name] of imports.matchAll(/^\s*Name: (.+\.dll)\s*$/gim)) {
            if (name !== basename(name)) throw new Error(`Invalid DLL import: ${name}`);
            const identity = name.toLowerCase();
            if (copied.has(identity)) continue;
            // API-set names are virtual Windows loader contracts, not disk files.
            if (/^(?:api|ext)-ms-/i.test(name) || existsSync(join(system, name))) continue;
            const destination = join(directory, name);
            if (!existsSync(destination)) {
                const source = join(toolchain, 'bin', name);
                if (!existsSync(source))
                    throw new Error(`Missing runtime DLL ${name}, imported by ${binary}`);
                await cp(source, destination);
            }
            copied.add(identity);
            pending.push(destination);
        }
    }
    await cp(join(toolchain, 'share/mingw32'), join(prefix, 'licenses/mingw-w64'), {
        recursive: true,
    });
}

// Linux's LLVM C++ runtime is not part of the base OS installation. Package the
// dependency closure alongside the executables and use relative loader paths.
export async function bundleRuntime(prefix) {
    if (process.platform === 'win32') {
        await bundleWindowsRuntime(prefix);
        await cp(new URL('./licenses/', import.meta.url), join(prefix, 'licenses/llvm'), {
            recursive: true,
        });
        return;
    }
    if (process.platform !== 'linux') return;
    const binaries = ['carven', 'carven-format', 'carven-analyzer', 'carven-lsp'].map((name) =>
        join(prefix, 'bin', name),
    );
    const libraries = new Map();
    const pending = [...binaries];
    await mkdir(join(prefix, 'lib'), { recursive: true });
    while (pending.length) {
        const dependencies = execFileSync('ldd', [pending.pop()], { encoding: 'utf8' });
        if (dependencies.includes('not found')) throw new Error(dependencies);
        for (const line of dependencies.split('\n')) {
            const match = line.match(/^\s*(lib(?:c\+\+|c\+\+abi|unwind)\.so(?:\.\d+)+) => (\S+)/);
            if (!match || libraries.has(match[1])) continue;
            const destination = join(prefix, 'lib', match[1]);
            if (resolve(match[2]) !== resolve(destination))
                await cp(match[2], destination, { dereference: true });
            libraries.set(match[1], destination);
            pending.push(destination);
        }
    }
    await cp(new URL('./licenses/', import.meta.url), join(prefix, 'licenses/llvm'), {
        recursive: true,
    });
    for (const binary of binaries)
        execFileSync('patchelf', ['--set-rpath', '$ORIGIN/../lib', binary]);
    for (const library of libraries.values())
        execFileSync('patchelf', ['--set-rpath', '$ORIGIN', library]);
}
