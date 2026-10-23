import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import * as lsp from 'vscode-languageserver-protocol';
import { Client, changes } from './client.ts';
import { createProject, localToolchain, position, temporary, uri } from './fixture.ts';

await temporary('refactor', async (root) => {
    const { source, library } = createProject(root, localToolchain());
    const libraryText = 'fn combine(left: i32, right: i64) -> i64 => right;';
    writeFileSync(library, libraryText);

    const consumer = join(root, 'src/consumer.cv');
    writeFileSync(consumer, 'import .helper using combine; fn consume() -> i64 => combine(1, 2);');

    let text =
        'import .helper using combine as compute; fn sample() { let _ = "😀"; let broken = ; compute(1, ';
    writeFileSync(source, text);

    const client = new Client();
    const at = (offset: number) => ({
        textDocument: { uri: uri(source) },
        position: position(text, offset),
    });

    try {
        await client.initialize(
            uri(root),
            {},
            {
                workspace: { workspaceEdit: { documentChanges: true } },
                textDocument: { rename: { prepareSupport: true } },
            },
        );
        await client.send('textDocument/didOpen', {
            textDocument: { uri: uri(source), version: 1, text },
        });

        let offset = text.lastIndexOf('compute');
        const prepared = await client.query(lsp.PrepareRenameRequest.type, at(offset));
        assert.deepEqual(prepared, {
            placeholder: 'compute',
            range: { start: position(text, offset), end: position(text, offset + 7) },
        });

        const help = await client.query(lsp.SignatureHelpRequest.type, at(text.length));
        assert(help);
        assert.equal(help.signatures[0].label, 'compute(left: i32, right: i64) -> i64');
        assert.equal(help.activeParameter, 1);

        const renamed = changes(
            (await client.query(lsp.RenameRequest.type, { ...at(offset), newName: 'calculate' })) ??
                undefined,
        );
        assert.equal(renamed.length, 1);
        assert.deepEqual(renamed[0].textDocument, { uri: uri(source), version: 1 });
        assert.equal(renamed[0].edits.length, 2);
        assert(renamed[0].edits.every((edit) => edit.newText === 'calculate'));
        assert.equal(
            (await client.response('textDocument/rename', { ...at(offset), newName: 'fn' })).error
                ?.code,
            -32602,
        );
        assert.equal(
            (await client.response('textDocument/rename', { ...at(offset), newName: 'sample' }))
                .error?.code,
            -32803,
        );

        text = text.replaceAll('compute', 'calculate');
        await client.send('textDocument/didChange', {
            textDocument: { uri: uri(source), version: 2 },
            contentChanges: [{ text }],
        });

        const exported = changes(
            (await client.query(lsp.RenameRequest.type, {
                ...at(text.indexOf('combine')),
                newName: 'merge',
            })) ?? undefined,
        );
        const byURI = Object.fromEntries(exported.map((item) => [item.textDocument.uri, item]));
        assert.deepEqual(
            new Set(Object.keys(byURI)),
            new Set([uri(source), uri(library), uri(consumer)]),
        );
        assert.equal(byURI[uri(source)].textDocument.version, 2);
        assert.equal(byURI[uri(library)].textDocument.version, null);
        assert.equal(byURI[uri(consumer)].textDocument.version, null);
        assert.equal(byURI[uri(source)].edits.length, 1);
        assert.equal(
            exported.reduce((sum, item) => sum + item.edits.length, 0),
            4,
        );

        const updated = await client.query(lsp.SignatureHelpRequest.type, at(text.length));
        assert(updated?.signatures[0].label.startsWith('calculate('));
        assert.equal(readFileSync(library, 'utf8'), libraryText);

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Refactoring: alias/declaration rename, unopened versioned edits, ' +
        'conflicts and incomplete signatures passed',
);
