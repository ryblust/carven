import { createVSIX } from '@vscode/vsce';
import { cp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = dirname(fileURLToPath(import.meta.url));
process.chdir(root);
await import('./build.mjs');

const manifest = JSON.parse(await readFile(join(root, 'package.json'), 'utf8'));
const output = join(root, '../../build/packages');
const stage = join(output, 'extension');

await rm(stage, { recursive: true, force: true });
await mkdir(join(stage, 'dist'), { recursive: true });
await mkdir(join(stage, 'resources'), { recursive: true });
for (const name of [
    'README.md',
    'LICENSE',
    'resources/icon.png',
    'language-configuration.json',
    'syntaxes',
    'dist/extension.cjs',
    'dist/THIRD_PARTY_NOTICES.txt',
]) {
    await cp(join(root, name), join(stage, name), { recursive: true });
}

// The bundled JS includes the language client; the toolchain is installed separately.
delete manifest.scripts;
delete manifest.dependencies;
delete manifest.devDependencies;
await writeFile(join(stage, 'package.json'), JSON.stringify(manifest, null, 2) + '\n');
await createVSIX({
    cwd: stage,
    dependencies: false,
    packagePath: join(output, `${manifest.name}-${manifest.version}.vsix`),
});
