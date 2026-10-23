import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import * as lsp from 'vscode-languageserver-protocol';
import { Client, actions, changes, completions, edits, locations } from './client.ts';
import { applyEdits, createProject, localToolchain, position, temporary, uri } from './fixture.ts';

await temporary('imports', async (root) => {
    const { source, library } = createProject(root, localToolchain());
    const libraryText =
        'fn combine() -> i32 => 1; fn alpha() -> i32 => 2; fn beta() -> i32 => 3; fn gamma() -> i32 => 4; struct Widget { value: i32 } private fn hidden() -> i32 => 5;';
    writeFileSync(library, libraryText);
    writeFileSync(join(root, 'src/extra.cv'), 'fn combine() -> i32 => 9;');
    writeFileSync(join(root, 'other.cv'), 'fn outside_target() -> i32 => 5;');

    const disk = '// 😀 header\r\nfn sample() -> i32 => combine();\r\n';
    writeFileSync(source, disk);

    let text = disk;
    let version = 1;
    const client = new Client();

    const at = (offset: number) => ({
        textDocument: { uri: uri(source) },
        position: position(text, offset),
    });

    const change = async (value: string) => {
        text = value;
        await client.send('textDocument/didChange', {
            textDocument: { uri: uri(source), version: ++version },
            contentChanges: [{ text }],
        });
    };

    const choices = async (offset = text.length) =>
        completions(await client.query(lsp.CompletionRequest.type, at(offset)));

    const fixes = async (kind: string) =>
        actions(
            await client.query(lsp.CodeActionRequest.type, {
                textDocument: { uri: uri(source) },
                range: { start: position(text, 0), end: position(text, text.length) },
                context: { diagnostics: [], only: [kind] },
            }),
        );

    const definition = async () =>
        locations(await client.query(lsp.DefinitionRequest.type, at(text.lastIndexOf('combine'))));

    try {
        await client.initialize(
            uri(root),
            {},
            {
                workspace: { workspaceEdit: { documentChanges: true } },
                textDocument: {
                    codeAction: {
                        codeActionLiteralSupport: {
                            codeActionKind: { valueSet: ['quickfix', 'source.organizeImports'] },
                        },
                    },
                },
            },
        );
        await client.send('textDocument/didOpen', {
            textDocument: { uri: uri(source), version, text },
        });

        await change('import .hel');
        let input = (await choices()).find((item) => item.label === '.helper');
        assert(input);
        assert.equal(input.kind, lsp.CompletionItemKind.Module);
        assert(!input.additionalTextEdits);
        await change(applyEdits(text, edits(input)) + ' using comb');
        input = (await choices()).find((item) => item.label === 'combine');
        assert(input);
        assert(!input.additionalTextEdits);
        await change(applyEdits(text, edits(input)) + '; fn sample() -> i32 => combine();');
        assert.equal((await definition())[0].uri, uri(library));

        await change('import .helper using { alpha, com');
        const selection = await choices();
        assert(selection.some((item) => item.label === 'combine'));
        assert(!selection.some((item) => item.label === 'alpha' || item.label === 'hidden'));

        await change('import');
        assert.equal((await choices()).length, 0);

        await change('import // comment');
        assert.equal((await choices()).length, 0);

        await change('import .helper using');
        assert.equal((await choices()).length, 0);

        await change('import .helper using alpha ');
        assert.equal((await choices()).length, 0);

        await change('import std::asy');
        assert((await choices()).some((item) => item.label === 'std::async'));

        await change('import std::core using Str');
        assert.equal(
            (await choices()).find((item) => item.label === 'String')?.kind,
            lsp.CompletionItemKind.Struct,
        );

        await change('import std::async using yie');
        assert((await choices()).some((item) => item.label === 'yield_once'));

        await change('fn sample() { ret }');
        assert(
            (await choices(text.indexOf('ret') + 3)).some(
                (item) => item.label === 'return' && item.kind === lsp.CompletionItemKind.Keyword,
            ),
        );

        await change(disk);
        const imports = (await fixes('quickfix')).filter((fix) =>
            fix.title.startsWith("Import 'combine'"),
        );
        assert.equal(imports.length, 2);
        const selectedFix = imports.find((fix) => fix.title.includes('helper'));
        assert(selectedFix);
        const document = changes(selectedFix.edit)[0];
        assert.deepEqual(document.textDocument, { uri: uri(source), version });
        await change(applyEdits(text, document.edits));
        assert.deepEqual(await fixes('quickfix'), []);
        assert.equal((await definition())[0].uri, uri(library));

        await change('import .helper using alpha;\r\nfn sample() { let broken = ; comb');
        let items = await choices();
        const candidates = items.filter((item) => item.label === 'combine');
        assert.equal(candidates.length, 2);
        const builtin = items.find((item) => item.label === 'yield_once');
        assert(builtin?.additionalTextEdits?.[0].newText.includes('std::async'));
        assert(!items.some((item) => ['hidden', 'outside_target'].includes(item.label)));
        let selected = candidates.find((item) => item.detail?.includes('helper'));
        assert(selected);
        assert.equal(selected.additionalTextEdits?.length, 1);
        await change(applyEdits(text, edits(selected)) + '(); }');
        assert(text.includes('{ alpha, combine }'));
        assert.equal((await definition())[0].uri, uri(library));

        await change('fn sample(value: Widgte) {}');
        items = await choices(text.indexOf('Widgte') + 3);
        assert(items.some((item) => item.label === 'Widget' && item.additionalTextEdits));
        assert(!items.some((item) => item.label === 'combine' && item.additionalTextEdits));

        await change('comb();');
        items = await choices(4);
        selected = items.find(
            (item) => item.label === 'combine' && item.detail?.includes('helper'),
        );
        assert(selected);
        assert.equal(selected.additionalTextEdits, undefined);
        assert.equal(selected.filterText, 'combine');
        await change(applyEdits(text, edits(selected)));
        assert(text.includes('using combine;') && text.endsWith('combine();'));

        await change(
            'import .helper using { gamma, beta as b, alpha }\r\nimport .helper using beta;\r\nfn sample() -> i32 => alpha() + b() + beta();\r\n',
        );
        let organized = await fixes('source.organizeImports');
        assert.equal(organized.length, 1);
        assert.equal(organized[0].kind, 'source.organizeImports');
        assert.equal(organized[0].diagnostics, undefined);
        await change(applyEdits(text, changes(organized[0].edit)[0].edits));
        assert(text.startsWith('import .helper using { alpha, beta, beta as b }\r\n'));
        assert.deepEqual(await fixes('source.organizeImports'), []);

        await change(
            'import .helper using { gamma, alpha }\r\nimport .helper using beta; // attached\r\nimport .helper using {\r\nfn sample() { let broken = ; alp',
        );
        organized = await fixes('source.organizeImports');
        assert.equal(organized.length, 1);
        await change(applyEdits(text, changes(organized[0].edit)[0].edits));
        assert(
            text.includes('{ alpha, gamma }') &&
                text.includes('using beta; // attached') &&
                text.includes('using {\r\nfn sample'),
        );
        assert(
            (await choices()).some((item) => item.label === 'combine' && item.additionalTextEdits),
        );

        writeFileSync(library, libraryText.replaceAll('combine', 'updated'));
        await client.send('workspace/didChangeWatchedFiles', {
            changes: [{ uri: uri(library), type: 2 }],
        });
        assert(
            (await choices()).some((item) => item.label === 'updated' && item.additionalTextEdits),
        );
        assert.equal(readFileSync(source, 'utf8'), disk);

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Imports: unopened declarations, import fixes/completions, aliases, target boundaries, ' +
        'recovery and dependency refresh passed',
);
