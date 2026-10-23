import { mkdirSync, mkdtempSync, realpathSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { TextDocument } from 'vscode-languageserver-textdocument';
import type { Position, TextEdit } from 'vscode-languageserver-protocol';

export const uri = (path: string): string => pathToFileURL(path).href;

export const position = (text: string, offset: number): Position =>
    TextDocument.create('', 'carven', 0, text).positionAt(offset);

export const applyEdits = (text: string, edits: TextEdit[]): string =>
    TextDocument.applyEdits(TextDocument.create('', 'carven', 0, text), edits);

export async function temporary(
    name: string,
    work: (root: string) => Promise<void>,
): Promise<void> {
    const root = realpathSync(mkdtempSync(join(tmpdir(), `carven-${name} 中文 `)));
    try {
        await work(root);
    } catch (error) {
        console.error('LSP test project retained:', root);
        throw error;
    }
    rmSync(root, { recursive: true, force: true });
}

type TestToolchain = { program: string; crafts: string };

export function localToolchain(): TestToolchain {
    const program = process.env.CARVEN_TEST_COMPILER;
    const crafts = process.env.CARVEN_TEST_CRAFTS;
    if (!program || !crafts) {
        throw new Error('Local editor tests require CARVEN_TEST_COMPILER and CARVEN_TEST_CRAFTS');
    }

    return { program: resolve(program), crafts: resolve(crafts) };
}

export function createProject(
    root: string,
    toolchain?: TestToolchain,
    rules = fileURLToPath(new URL('../../../../xmake/carven', import.meta.url)),
) {
    const configure = toolchain
        ? `on_config(function (target)
    if not target:rule("carven") then return end
    target:extraconf_set("rules", "carven", "toolchain", {
        program = ${JSON.stringify(toolchain.program)},
        crafts = ${JSON.stringify(toolchain.crafts)},
    })
end)
`
        : '';

    mkdirSync(join(root, 'src'));
    writeFileSync(
        join(root, 'xmake.lua'),
        `includes(${JSON.stringify(rules)})
${configure}target("app")
    set_kind("binary")
    add_rules("carven")
    add_files("src/*.cv")
target("other")
    set_default(false)
    set_kind("binary")
    add_rules("carven")
    add_files("other.cv")
`,
    );
    writeFileSync(join(root, 'other.cv'), 'fn broken( {');

    const source = join(root, 'src/main.cv');
    const library = join(root, 'src/helper.cv');
    const text =
        'import .helper using provide;\r\nfn sample() { let _ = "😀"; let value = provide(); println(value); }';
    writeFileSync(library, 'fn provide() -> i32 => true;');
    writeFileSync(source, text);
    return { source, library, text };
}
