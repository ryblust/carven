import assert from 'node:assert/strict';
import { isDeepStrictEqual } from 'node:util';
import { writeFileSync } from 'node:fs';
import * as lsp from 'vscode-languageserver-protocol';
import { Client } from './client.ts';
import { createProject, localToolchain, position, temporary, uri } from './fixture.ts';

await temporary('structure', async (root) => {
    const { source, library } = createProject(root, localToolchain());
    writeFileSync(library, 'fn provide() -> i32 {\r\n    return 1;\r\n}\r\n');

    let text =
        '// 😀 heading\r\n// detail\r\nimport .helper using provide as compute;\r\nimport .helper using provide as again;\r\nfn sample() {\r\n    let broken = "unfinished\r\n    let chosen = compute();\r\n    if true {\r\n        let chosen = again();\r\n        println(chosen);\r\n    }\r\n    println("😀"); println(chosen);\r\n    let values = [\r\n        1,\r\n        2\r\n    ];\r\n    compute(\r\n';
    writeFileSync(source, text);

    let client = new Client();

    const highlight = (offset: number) =>
        client.query(lsp.DocumentHighlightRequest.type, {
            textDocument: { uri: uri(source) },
            position: position(text, offset),
        });

    const folding = (path = source) =>
        client.query(lsp.FoldingRangeRequest.type, { textDocument: { uri: uri(path) } });

    try {
        await client.initialize(
            uri(root),
            {},
            { textDocument: { foldingRange: { lineFoldingOnly: true } } },
        );
        await client.send('textDocument/didOpen', {
            textDocument: { uri: uri(source), version: 1, text },
        });

        const highlights = await highlight(text.lastIndexOf('compute'));
        assert(highlights);
        const expected = [...text.matchAll(/compute/g)].map((match) => position(text, match.index));
        assert.deepEqual(
            highlights.map((item) => item.range.start),
            expected,
        );

        const local = await highlight(text.lastIndexOf('chosen'));
        assert(local);
        assert.equal(local.length, 2);
        assert.deepEqual(local[0].range.start, position(text, text.indexOf('chosen')));
        assert.deepEqual(await highlight(text.indexOf('heading')), []);

        const folds = await folding();
        assert(folds);
        for (const expected of [
            { startLine: 0, endLine: 1, kind: 'comment' },
            { startLine: 2, endLine: 3, kind: 'imports' },
            { startLine: 7, endLine: 9 },
            { startLine: 12, endLine: 14 },
        ]) {
            assert(
                folds.some((fold) => isDeepStrictEqual(fold, expected)),
                JSON.stringify(folds),
            );
        }
        assert(
            folds.every(
                (fold) => fold.startCharacter === undefined && fold.endCharacter === undefined,
            ),
        );
        assert.deepEqual(await folding(library), [{ startLine: 0, endLine: 1 }]);

        text = 'fn fresh() {\r\n    println(1);\r\n}\r\n';
        await client.send('textDocument/didChange', {
            textDocument: { uri: uri(source), version: 2 },
            contentChanges: [{ text }],
        });
        assert.deepEqual(await folding(), [{ startLine: 0, endLine: 1 }]);

        await client.finish();
    } finally {
        await client.close();
    }

    writeFileSync(source, '// 😀 header\r\n// detail\r\nfn sample() {\r\n    println(1);\r\n}\r\n');
    client = new Client();

    try {
        await client.initialize(
            uri(root),
            {},
            { textDocument: { foldingRange: { rangeLimit: 1 } } },
        );
        assert.deepEqual(await folding(), [
            { startLine: 0, startCharacter: 12, endLine: 1, endCharacter: 9, kind: 'comment' },
        ]);

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Structure: alias/shadow highlights, recovered syntax, unopened folds, ' +
        'line-only and UTF-16 folding passed',
);
