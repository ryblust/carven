import { build } from 'esbuild';
import { existsSync } from 'node:fs';
import { readFile, readdir, writeFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('.', import.meta.url));

await build({
    absWorkingDir: root,
    entryPoints: ['src/extension.cjs'],
    bundle: true,
    platform: 'node',
    format: 'cjs',
    external: ['vscode'],
    outfile: 'dist/extension.cjs',
});

if (process.argv.includes('--tests')) {
    await build({
        absWorkingDir: root,
        entryPoints: ['tests/integration.ts'],
        bundle: true,
        platform: 'node',
        format: 'cjs',
        external: ['vscode'],
        outfile: 'dist/tests/integration.cjs',
        sourcemap: true,
    });
}

const require = createRequire(import.meta.url);

const notices = [];
const visited = new Set();

async function collectLicenses(name, resolver) {
    let directory = dirname(resolver.resolve(name));
    while (
        !existsSync(join(directory, 'package.json')) ||
        JSON.parse(await readFile(join(directory, 'package.json'), 'utf8')).name !== name
    ) {
        const parent = dirname(directory);
        if (parent === directory) throw new Error('Package root not found for ' + name);
        directory = parent;
    }

    const manifestPath = join(directory, 'package.json');
    if (visited.has(manifestPath)) return;
    visited.add(manifestPath);

    const manifest = JSON.parse(await readFile(manifestPath, 'utf8'));
    const license = (await readdir(directory)).find((file) => /^licen[cs]e(?:\.|$)/i.test(file));
    if (!license) throw new Error('Missing license for ' + name);

    notices.push(
        `${manifest.name} ${manifest.version}\n\n${await readFile(join(directory, license), 'utf8')}`,
    );
    for (const dependency of Object.keys(manifest.dependencies ?? {})) {
        await collectLicenses(dependency, createRequire(manifestPath));
    }
}

await collectLicenses('vscode-languageclient', require);
await writeFile(join(root, 'dist/THIRD_PARTY_NOTICES.txt'), notices.join('\n\n---\n\n'));
