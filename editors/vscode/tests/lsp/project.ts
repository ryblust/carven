import assert from 'node:assert/strict';
import { existsSync, mkdirSync, readFileSync, unlinkSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import * as lsp from 'vscode-languageserver-protocol';
import { Client, changes, hoverText, locations } from './client.ts';
import { createProject, localToolchain, position, temporary, uri } from './fixture.ts';

await temporary('project', async (root) => {
    const fixture = createProject(root, localToolchain());
    const { source, library } = fixture;
    let text = fixture.text;
    const diskLibrary = uri(library);
    let openLibrary = diskLibrary.replace('/helper.cv', '/%68elper.cv');
    if (process.platform === 'win32') {
        openLibrary = openLibrary.replace(
            /^file:\/\/\/([a-z]):/i,
            (_, drive: string) => `file:///${drive.toLowerCase()}%3A`,
        );
    }

    const client = new Client();

    const notification = async (
        destination: string,
        version?: number,
        errors?: boolean,
    ): Promise<lsp.PublishDiagnosticsParams> => {
        for (;;) {
            const message = await client.receive();
            assert.notEqual(message.method, 'window/logMessage', JSON.stringify(message));
            if (message.method !== 'textDocument/publishDiagnostics') continue;
            const params = message.params as lsp.PublishDiagnosticsParams;
            assert(params.uri.startsWith(uri(root)) || params.uri === openLibrary);
            assert.notEqual(params.uri, uri(join(root, 'other.cv')));
            if (params.uri !== destination || (version !== undefined && params.version !== version))
                continue;
            if (errors !== undefined && Boolean(params.diagnostics.length) !== errors) {
                assert.equal(version, undefined, JSON.stringify(params));
                continue;
            }

            return params;
        }
    };

    const changed = (path: string, type: lsp.FileChangeType = 2) =>
        client.send('workspace/didChangeWatchedFiles', { changes: [{ uri: uri(path), type }] });

    const hover = async (expected: string) =>
        assert.equal(
            hoverText(
                await client.query(lsp.HoverRequest.type, {
                    textDocument: { uri: uri(source) },
                    position: position(text, text.indexOf('value')),
                }),
            ),
            expected,
        );

    try {
        await client.initialize(
            uri(root),
            {},
            { workspace: { workspaceEdit: { documentChanges: true } } },
        );
        const diagnostics = await notification(uri(library), undefined, true);
        assert.equal(diagnostics.version, undefined);
        await client.send('textDocument/didOpen', {
            textDocument: { uri: uri(source), version: 1, text },
        });
        await notification(uri(source), 1);

        writeFileSync(library, 'fn provide() -> i64 => 42;');
        await changed(library);
        await notification(uri(library), undefined, false);
        await hover('i64');
        const at = text.indexOf('provide();');

        await client.send('textDocument/didChange', {
            textDocument: { uri: uri(source), version: 2 },
            contentChanges: [
                {
                    range: {
                        start: position(text, at),
                        end: position(text, at + 'provide()'.length),
                    },
                    text: 'provide() as i32',
                },
                {
                    range: { start: { line: 0, character: 0 }, end: { line: 0, character: 0 } },
                    text: '// 😀\r\n',
                },
            ],
        });
        text = '// 😀\r\n' + text.replace('provide();', 'provide() as i32;');
        await notification(uri(source), 2, false);
        await hover('i32');
        await client.send('textDocument/didOpen', {
            textDocument: { uri: openLibrary, version: 1, text: 'fn provide() -> i32 => true;' },
        });
        await notification(openLibrary, 1, true);
        await client.send('textDocument/didChange', {
            textDocument: { uri: openLibrary, version: 2 },
            contentChanges: [{ text: 'fn provide() -> i32 => 7;' }],
        });
        await notification(openLibrary, 2, false);

        const use = {
            textDocument: { uri: uri(source) },
            position: position(text, text.lastIndexOf('provide')),
        };
        const definition = locations(await client.query(lsp.DefinitionRequest.type, use));
        assert.deepEqual(
            definition.map((location) => location.uri),
            [openLibrary],
        );
        const renamed = changes(
            (await client.query(lsp.RenameRequest.type, { ...use, newName: 'supply' })) ??
                undefined,
        );
        assert.deepEqual(
            new Set(renamed.map((document) => document.textDocument.uri)),
            new Set([openLibrary, uri(source)]),
        );
        assert.equal(
            renamed.find((document) => document.textDocument.uri === openLibrary)?.textDocument
                .version,
            2,
        );

        writeFileSync(library, 'fn provide() -> i64 => true;');
        await client.send('textDocument/didClose', { textDocument: { uri: openLibrary } });
        await notification(diskLibrary, undefined, true);
        const closedDefinition = locations(await client.query(lsp.DefinitionRequest.type, use));
        assert.deepEqual(
            closedDefinition.map((location) => location.uri),
            [diskLibrary],
        );

        writeFileSync(library, 'fn provide() -> i64 => 42;');
        await changed(library);
        await notification(diskLibrary, undefined, false);

        const created = join(root, 'src/created.cv');
        writeFileSync(created, 'fn added() -> i32 => true;');
        await changed(created, 1);
        await notification(uri(created), undefined, true);

        unlinkSync(created);
        await changed(created, 3);
        await notification(uri(created), undefined, false);

        await client.send('textDocument/didClose', { textDocument: { uri: uri(source) } });
        await notification(uri(source), undefined, false);
        text = readFileSync(source, 'utf8');
        await client.send('textDocument/didOpen', {
            textDocument: { uri: uri(source), version: 1, text },
        });
        await notification(uri(source), 1, false);
        await hover('i64');

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Project: unopened diagnostics/imports, incremental edits, disk refresh, URI-equivalent overlays, ' +
        'glob membership and target isolation passed',
);

await temporary('native-project', async (root) => {
    mkdirSync(join(root, 'src'));
    const source = join(root, 'src/main.cv');
    const library = join(root, 'src/helper.cv');
    const dependency = join(root, 'src/dependency.cv');
    const text =
        'import .helper using answer;\nfn damaged() -> i32 => true;\nfn sample() -> i64 => answer();';
    writeFileSync(source, text);

    writeFileSync(library, 'fn answer() -> i64 => 42;');
    writeFileSync(dependency, 'fn dependency() -> i32 => true;');
    writeFileSync(join(root, 'src/native.cpp'), 'int native_value = 42;');
    writeFileSync(
        join(root, 'build.cv'),
        `import std::build using project;
project({targets: [
    {name: "support", kind: .StaticLibrary, sources: ["src/dependency.cv"], ...},
    {name: "native", kind: .Executable, sources: ["src/main.cv", "src/native.cpp"], dependencies: ["support"], ...}
], ...})?;
`,
    );

    const client = new Client();
    const diagnostics = new Map<string, lsp.Diagnostic[]>();
    client.observe = (message) => {
        assert.equal(message.method, 'textDocument/publishDiagnostics', JSON.stringify(message));
        const published = message.params as lsp.PublishDiagnosticsParams;
        diagnostics.set(published.uri, published.diagnostics);
    };

    try {
        await client.initialize(uri(join(root, 'src')), { target: 'native' });
        const symbols = await client.query(lsp.WorkspaceSymbolRequest.type, { query: 'answer' });
        assert(
            symbols?.some(
                (symbol) => symbol.name === 'answer' && symbol.location.uri === uri(library),
            ),
        );
        assert(diagnostics.get(uri(source))?.some((item) => item.code === 'CV-TYPE-MISMATCH'));
        assert(diagnostics.get(uri(dependency))?.some((item) => item.code === 'CV-TYPE-MISMATCH'));
        await client.send('textDocument/didOpen', {
            textDocument: { uri: uri(source), version: 1, text },
        });
        const definitions = await client.query(lsp.DefinitionRequest.type, {
            textDocument: { uri: uri(source) },
            position: position(text, text.lastIndexOf('answer')),
        });
        assert(
            Array.isArray(definitions) &&
                definitions.some((item) => 'uri' in item && item.uri === uri(library)),
        );
        assert(!existsSync(join(root, 'build')));

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Native project: mixed roots, target dependencies, and imported helpers remain available with diagnostics',
);
