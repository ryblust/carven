import assert from 'node:assert/strict';
import { mkdirSync, symlinkSync, unlinkSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import * as lsp from 'vscode-languageserver-protocol';
import { Client, changes, hoverText, locations } from './client.ts';
import { createProject, localToolchain, position, temporary, uri } from './fixture.ts';

await temporary('document-aliases', async (root) => {
    const { source, library, text } = createProject(root, localToolchain());
    const declaration = (type: string) =>
        `fn provide() -> ${type} { let value: ${type} = 42; return value; }`;
    const diskText = declaration('i64');
    writeFileSync(library, diskText);
    const alias = join(root, 'linked-src');
    const linkKind = process.platform === 'win32' ? 'junction' : 'dir';
    symlinkSync(join(root, 'src'), alias, linkKind);
    const primary = uri(library);
    const secondary = uri(join(alias, 'helper.cv')).replace('/helper.cv', '/%68elper.cv');
    const client = new Client();
    const published = new Map<string, lsp.PublishDiagnosticsParams>();
    client.observe = (message) => {
        assert.equal(message.method, 'textDocument/publishDiagnostics', JSON.stringify(message));
        const params = message.params as lsp.PublishDiagnosticsParams;
        published.set(params.uri, params);
    };

    const open = (destination: string, version: number, value: string) =>
        client.send('textDocument/didOpen', {
            textDocument: { uri: destination, version, text: value },
        });
    const change = (destination: string, version: number, value: string) =>
        client.send('textDocument/didChange', {
            textDocument: { uri: destination, version },
            contentChanges: [{ text: value }],
        });
    const close = (destination: string) =>
        client.send('textDocument/didClose', { textDocument: { uri: destination } });
    const hover = async (destination: string, value: string) =>
        hoverText(
            await client.query(lsp.HoverRequest.type, {
                textDocument: { uri: destination },
                position: position(value, value.indexOf('value')),
            }),
        );
    const sourceHover = () => hover(uri(source), text);

    try {
        await client.initialize(
            uri(root),
            {},
            { workspace: { workspaceEdit: { documentChanges: true } } },
        );
        await open(uri(source), 1, text);
        const sharedText = diskText.replace('42', 'true');
        await open(primary, 10, sharedText);
        await open(secondary, 1, sharedText);
        assert.equal(await sourceHover(), 'i64');
        assert.equal(published.get(primary)?.version, 10);
        assert.equal(published.get(secondary)?.version, 1);
        for (const destination of [primary, secondary]) {
            assert(
                published
                    .get(destination)
                    ?.diagnostics.some((item) => item.code === 'CV-TYPE-MISMATCH'),
            );
        }

        const changedText = declaration('i32');
        await change(secondary, 2, changedText);
        assert.equal(await hover(primary, sharedText), 'i64');
        assert.equal(await hover(secondary, changedText), 'i32');
        for (const [destination, version] of [
            [primary, 10],
            [secondary, 2],
        ] as const) {
            const diagnostics = published.get(destination);
            assert.equal(diagnostics?.version, version);
            assert(diagnostics?.diagnostics.some((item) => item.code === 'CV-DOCUMENT-CONFLICT'));
        }
        const rejected = await client.response(lsp.HoverRequest.type.method, {
            textDocument: { uri: uri(source) },
            position: position(text, text.indexOf('value')),
        });
        assert.equal(rejected.error?.code, -32803);
        const rename = await client.response(lsp.RenameRequest.type.method, {
            textDocument: { uri: secondary },
            position: position(changedText, changedText.indexOf('provide')),
            newName: 'supply',
        });
        assert.equal(rename.error?.code, -32803);
        const localSymbols = await client.query(lsp.DocumentSymbolRequest.type, {
            textDocument: { uri: secondary },
        });
        assert(localSymbols?.some((item) => item.name === 'provide'));

        await client.send('workspace/didChangeWatchedFiles', {
            changes: [{ uri: uri(join(root, 'xmake.lua')), type: 2 }],
        });
        assert.equal(await hover(secondary, changedText), 'i32');

        await change(primary, 11, changedText);
        assert.equal(await sourceHover(), 'i32');
        for (const destination of [primary, secondary]) {
            assert.deepEqual(published.get(destination)?.diagnostics, [], destination);
        }
        const edits = changes(
            (await client.query(lsp.RenameRequest.type, {
                textDocument: { uri: uri(source) },
                position: position(text, text.lastIndexOf('provide')),
                newName: 'supply',
            })) ?? undefined,
        );
        const helperEdit = edits.find(
            (item) => item.textDocument.uri === primary || item.textDocument.uri === secondary,
        );
        assert(helperEdit);
        assert.equal(
            helperEdit.textDocument.version,
            helperEdit.textDocument.uri === primary ? 11 : 2,
        );

        await close(primary);
        const definitions = locations(
            await client.query(lsp.DefinitionRequest.type, {
                textDocument: { uri: uri(source) },
                position: position(text, text.lastIndexOf('provide')),
            }),
        );
        assert.deepEqual(
            definitions.map((item) => item.uri),
            [secondary],
        );
        await change(secondary, 3, declaration('i16'));
        assert.equal(await sourceHover(), 'i16');
        await close(secondary);
        assert.equal(await sourceHover(), 'i64');

        await open(secondary, 1, declaration('i8'));
        assert.equal(await sourceHover(), 'i8');
        mkdirSync(join(root, 'replacement'));
        writeFileSync(join(root, 'replacement/helper.cv'), declaration('i16'));
        unlinkSync(alias);
        symlinkSync(join(root, 'replacement'), alias, linkKind);
        await change(secondary, 2, changedText);
        assert.equal(await sourceHover(), 'i32');
        await close(secondary);
        assert.equal(await sourceHover(), 'i64');
        await open(secondary, 1, declaration('i16'));
        assert.equal(await hover(secondary, declaration('i16')), 'i16');
        assert.equal(await sourceHover(), 'i64');
        await close(secondary);

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Document aliases: independent versions, conflict recovery, close/reopen and stable bindings passed',
);

await temporary('standalone-documents', async (root) => {
    const client = new Client();
    const inputs = [
        { uri: 'untitled:standalone', type: 'i8' },
        { uri: uri(join(root, '..', 'external-first', 'same.cv')), type: 'i32' },
        { uri: uri(join(root, '..', 'external-second', 'same.cv')), type: 'i64' },
    ];
    const text = (type: string) => `fn sample() { let value: ${type} = 42; }`;
    try {
        await client.initialize(uri(root));
        for (const input of inputs) {
            await client.send('textDocument/didOpen', {
                textDocument: { uri: input.uri, version: 1, text: text(input.type) },
            });
        }
        for (const input of inputs) {
            const value = text(input.type);
            const hover = await client.query(lsp.HoverRequest.type, {
                textDocument: { uri: input.uri },
                position: position(value, value.indexOf('value')),
            });
            assert.equal(hoverText(hover), input.type, input.uri);
        }
        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Standalone documents: untitled buffers and same-named external files remain independent',
);
