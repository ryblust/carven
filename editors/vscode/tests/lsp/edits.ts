import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import * as lsp from 'vscode-languageserver-protocol';
import { Client, actions, changes } from './client.ts';
import { applyEdits, createProject, localToolchain, position, temporary, uri } from './fixture.ts';

await temporary('edits', async (root) => {
    const { source, library } = createProject(root, localToolchain());
    writeFileSync(library, 'fn combine() -> i32 => 1;');
    const disk =
        '// 😀 comment\r\nimport .helper using combine; fn sample( )->i32{return comibne();}\r\n';
    writeFileSync(source, disk);

    let text = disk;
    let version = 1;
    const client = new Client();

    const change = async (value: string) => {
        text = value;
        await client.send('textDocument/didChange', {
            textDocument: { uri: uri(source), version: ++version },
            contentChanges: [{ text }],
        });
    };

    const format = () =>
        client.query(lsp.DocumentFormattingRequest.type, {
            textDocument: { uri: uri(source) },
            options: { tabSize: 4, insertSpaces: true },
        });

    const fixes = async (only?: string[]) =>
        actions(
            await client.query(lsp.CodeActionRequest.type, {
                textDocument: { uri: uri(source) },
                range: { start: position(text, 0), end: position(text, text.length) },
                context: { diagnostics: [], ...(only ? { only } : {}) },
            }),
        );

    try {
        await client.initialize(
            uri(root),
            {},
            {
                workspace: { workspaceEdit: { documentChanges: true } },
                textDocument: {
                    codeAction: {
                        codeActionLiteralSupport: { codeActionKind: { valueSet: ['quickfix'] } },
                    },
                },
            },
        );
        await client.send('textDocument/didOpen', {
            textDocument: { uri: uri(source), version, text },
        });

        const formattedEdits = await format();
        assert(formattedEdits);
        assert.equal(formattedEdits.length, 1);
        assert.deepEqual(formattedEdits[0].range.start, position(text, '// 😀 comment'.length));
        const formatted = applyEdits(text, formattedEdits);
        assert(
            formatted.startsWith('// 😀 comment\n') &&
                formatted.endsWith('}\n') &&
                formatted.includes('return comibne();'),
        );
        await change(formatted);
        assert.deepEqual(await format(), []);
        assert.deepEqual(await fixes(['source']), []);

        const suggestions = await fixes(['quickfix']);
        assert.equal(suggestions.length, 1);
        assert.equal(suggestions[0].title, "Change to 'combine'");
        const diagnostic = suggestions[0].diagnostics?.[0];
        assert(
            diagnostic &&
                typeof diagnostic.message === 'string' &&
                diagnostic.message.startsWith('unresolved name'),
        );
        const document = changes(suggestions[0].edit)[0];
        assert.deepEqual(document.textDocument, { uri: uri(source), version });
        await change(applyEdits(text, document.edits));
        assert.deepEqual(await fixes(), []);

        for (const [broken, replacement] of [
            ['fn sample() -> i32 { return 1\n}', ';'],
            ['fn sample() { let _ = (1; }', ')'],
            ['fn sample() { let _ = [1, 2; }', ']'],
        ]) {
            await change(broken);
            const choices = await fixes();
            assert.equal(choices.length, 1);
            const edits = changes(choices[0].edit)[0].edits;
            assert.equal(edits[0].newText, replacement);
            await change(applyEdits(text, edits));
            assert.deepEqual(await fixes(), []);
        }

        await change('fn sample( {');
        assert.equal(
            (
                await client.response('textDocument/formatting', {
                    textDocument: { uri: uri(source) },
                    options: { tabSize: 4, insertSpaces: true },
                })
            ).error?.code,
            -32803,
        );
        assert.equal(readFileSync(source, 'utf8'), disk);

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Edits: canonical formatting, UTF-16 edits, imported spelling and delimiter fixes passed',
);
