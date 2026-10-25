import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { cp, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { basename, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { bundleRuntime } from './runtime.mjs';

const [prefix, outputArgument] = process.argv.slice(2);
if (!prefix)
    throw new Error('Usage: node tools/release/package.mjs installed-prefix [output-directory]');

const platform = `${process.platform}-${process.arch}`;
if (
    ![
        'darwin-arm64',
        'darwin-x64',
        'linux-arm64',
        'linux-x64',
        'win32-arm64',
        'win32-x64',
    ].includes(platform)
) {
    throw new Error(`Unsupported toolchain platform: ${platform}`);
}

const source = resolve(prefix);
const suffix = process.platform === 'win32' ? '.exe' : '';

const version = execFileSync(join(source, 'bin', 'carven' + suffix), ['--version'], {
    encoding: 'utf8',
})
    .trim()
    .match(/^carven v(\d+\.\d+\.\d+)$/)?.[1];

if (!version) throw new Error('Unrecognized compiler version');

const output = resolve(
    outputArgument ?? fileURLToPath(new URL('../../build/packages', import.meta.url)),
);

const stage = join(output, platform, 'toolchain');
await rm(stage, { recursive: true, force: true });
await mkdir(stage, { recursive: true });
await cp(source, stage, { recursive: true, dereference: true });
await bundleRuntime(stage);
await writeFile(join(stage, '.carven-toolchain'), `${version}\n`);

const archive = join(output, `carven-${platform}.tar.gz`);
const tar = process.platform === 'win32' ? join(process.env.SystemRoot, 'System32/tar.exe') : 'tar';

execFileSync(tar, ['-czf', relative(stage, archive), '.'], {
    cwd: stage,
    stdio: 'inherit',
    env: { ...process.env, COPYFILE_DISABLE: '1' },
});

const digest = createHash('sha256')
    .update(await readFile(archive))
    .digest('hex');

await writeFile(archive + '.sha256', `${digest}  ${basename(archive)}\n`);

console.log(archive);
