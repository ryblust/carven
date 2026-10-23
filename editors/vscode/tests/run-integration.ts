import { spawn, spawnSync } from 'node:child_process';
import { mkdtemp, mkdir, readFile, realpath, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createProject, localToolchain } from './lsp/fixture.ts';

const vsix = process.env.CARVEN_VSIX;
const toolchain = vsix ? undefined : localToolchain();
if (!process.env.CARVEN_LSP) throw new Error('Editor tests require CARVEN_LSP');
const rules = vsix
    ? resolve(dirname(await realpath(process.env.CARVEN_LSP)), '../share/xmake/carven')
    : undefined;

const executable = process.env.VSCODE_EXECUTABLE ?? 'code';
if (process.platform === 'win32' && !process.env.VSCODE_EXECUTABLE) {
    throw new Error('Set VSCODE_EXECUTABLE to the VS Code Code.exe executable on Windows');
}

const root = fileURLToPath(new URL('../', import.meta.url));
const profile = await realpath(await mkdtemp(join(tmpdir(), 'carven-lsp-vscode-')));
let passed = false;

try {
    await mkdir(join(profile, 'User'));
    await writeFile(
        join(profile, 'User', 'settings.json'),
        JSON.stringify({
            'carven.serverPath': vsix ? undefined : resolve(process.env.CARVEN_LSP!),
            'carven.xmakePath': process.env.CARVEN_XMAKE ?? 'xmake',
            'security.workspace.trust.enabled': false,
            'window.restoreWindows': 'none',
            // Keep standard LSP traces with failed profiles for scheduling investigations.
            'carven.trace.server': 'verbose',
        }),
    );

    let development = root;
    if (vsix) {
        const installed = spawnSync(
            executable,
            [
                '--install-extension',
                resolve(vsix),
                '--force',
                '--user-data-dir',
                profile,
                '--extensions-dir',
                join(profile, 'extensions'),
            ],
            { stdio: 'inherit', timeout: 60000 },
        );
        if (installed.error) throw installed.error;
        if (installed.status !== 0)
            throw new Error('VSIX installation failed: ' + installed.status);

        // Run the same host tests alongside the installed extension, not a source override.
        development = join(profile, 'test-driver');
        await mkdir(development);
        await writeFile(
            join(development, 'package.json'),
            JSON.stringify({
                name: 'carven-tests',
                publisher: 'carven-lang',
                version: '0.0.0',
                engines: { vscode: '^1.142.0' },
            }),
        );
    }

    const workspace = join(profile, 'workspace');
    await mkdir(workspace);
    createProject(workspace, toolchain, rules);
    const config = join(workspace, 'xmake.lua');
    const configured = await readFile(config, 'utf8');
    const split = configured.indexOf('target("app")');
    const included = join(profile, 'targets.lua');
    await writeFile(
        included,
        configured
            .slice(split)
            .replace('"src/*.cv"', JSON.stringify(join(workspace, 'src/*.cv')))
            .replace('"other.cv"', JSON.stringify(join(workspace, 'other.cv'))),
    );
    await writeFile(
        config,
        configured.slice(0, split) + 'includes(' + JSON.stringify(included) + ')\n',
    );

    const child = spawn(
        executable,
        [
            join(workspace, 'src'),
            '--wait',
            '--new-window',
            '--user-data-dir',
            profile,
            '--extensions-dir',
            join(profile, 'extensions'),
            '--skip-welcome',
            '--skip-release-notes',
            '--disable-updates',
            '--log',
            'trace',
            '--extensionDevelopmentPath=' + development,
            '--extensionTestsPath=' + join(root, 'dist', 'tests', 'integration.cjs'),
        ],
        {
            stdio: 'inherit',
            env: { ...process.env, CARVEN_LSP_TEST_RESULT: join(profile, 'result') },
        },
    );
    const timeout = setTimeout(() => child.kill(), 60000);

    try {
        await new Promise<void>((resolve, reject) => {
            child.on('error', reject);
            child.on('exit', (code, signal) =>
                code === 0
                    ? resolve()
                    : reject(new Error(`VS Code tests exited ${code ?? signal}`)),
            );
        });

        const result = await readFile(join(profile, 'result'), 'utf8');
        if (result !== 'passed') throw new Error(result);
        passed = true;
        console.log(
            'VS Code: unopened project diagnostics, definitions, reverse references, completion, ' +
                'auto imports, organize imports, formatting, quick fixes, symbols, document highlights, ' +
                'folding ranges, semantic tokens, inlay hints, build-context refresh, rename, ' +
                'signature help, continuous editor edits, incremental edits, configuration recovery ' +
                'with unsaved/dependency changes and file/untitled hover passed',
        );
    } finally {
        clearTimeout(timeout);
    }
} finally {
    if (passed) {
        await rm(profile, { recursive: true, force: true });
    } else {
        console.error('VS Code test profile retained:', profile);
    }
}
