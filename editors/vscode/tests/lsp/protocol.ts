import assert from 'node:assert/strict';
import { existsSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Client, frame, hover, until } from './client.ts';
import type { Hover, PublishDiagnosticsParams } from 'vscode-languageserver-protocol';
import { hoverText } from './client.ts';
import { temporary } from './fixture.ts';

let client = new Client();

try {
    await client.send('unknown', {}, 0);
    assert.equal((await client.receive()).error?.code, -32002);

    client.child.stdin.write(Buffer.from('Content-Length: 1\r\n\r\n{'));
    assert.equal((await client.receive()).error?.code, -32700);

    await client.initialize(
        undefined,
        {},
        { textDocument: { publishDiagnostics: { relatedInformation: true } } },
    );
    await client.send('initialize', { capabilities: {} }, 'repeat-initialize');
    assert.equal((await client.receive()).error?.code, -32600);

    client.child.stdin.write(frame([]));
    assert.equal((await client.receive()).error?.code, -32600);

    await client.send('textDocument/hover', {}, 'invalid-params');
    assert.equal((await client.receive()).error?.code, -32602);

    const uri = 'file:///workspace/main.cv';
    const text = 'fn main() { let _ = "中文😀"; let value: i32 = 42; println(value); }';
    await client.send(
        'textDocument/didOpen',
        { textDocument: { uri, languageId: 'carven', version: 1, text } },
        undefined,
        true,
    );
    assert.deepEqual(await client.diagnostics(uri, 1), []);
    await hover(client, uri, text, 'value:', 2, 'i32');

    await client.send(
        'textDocument/hover',
        { textDocument: { uri }, position: { line: 0, character: 10000 } },
        'invalid-position',
    );
    assert.equal((await client.receive()).result, null);

    const changed = text.replace('value: i32', 'value: i64');

    const change = (version: number, value: string) =>
        client.send('textDocument/didChange', {
            textDocument: { uri, version },
            contentChanges: [{ text: value }],
        });

    await change(2, changed);
    assert.deepEqual(await client.diagnostics(uri, 2), []);
    await hover(client, uri, changed, 'value:', 3, 'i64');

    await change(1, text);
    await hover(client, uri, changed, 'value:', 4, 'i64');

    await change(3, 'fn main() { let value: i32 = true; }');
    const diagnostics = await client.diagnostics(uri, 3);
    assert(diagnostics.some((item) => item.severity === 1));
    assert(diagnostics.every((item) => item.source === 'carven' && item.message));

    await change(4, changed);
    assert.deepEqual(await client.diagnostics(uri, 4), []);
    await hover(client, uri, changed, 'value:', 5, 'i64');

    await change(5, 'fn repeated() {} fn repeated() {}');
    const duplicate = (await client.diagnostics(uri, 5)).find((item) => item.code === 'CV-CATALOG');
    assert(duplicate);
    assert(
        typeof duplicate.message === 'string' &&
            duplicate.message.includes('duplicate declaration'),
    );
    assert.deepEqual(duplicate.relatedInformation, [
        {
            location: {
                uri,
                range: { start: { line: 0, character: 3 }, end: { line: 0, character: 11 } },
            },
            message: 'first declaration',
        },
    ]);

    await client.send('unknown', {}, 6);
    assert.equal((await client.receive()).error?.code, -32601);

    await client.send('textDocument/didClose', { textDocument: { uri } });
    assert.deepEqual(((await client.receive()).params as PublishDiagnosticsParams).diagnostics, []);

    await client.send(
        'textDocument/hover',
        { textDocument: { uri }, position: { line: 0, character: 0 } },
        7,
    );
    assert.equal((await client.receive()).result, null);

    const untitled = 'untitled:Untitled-1';
    for (const value of ['fn main( {', 'fn main() {}']) {
        await client.send('textDocument/didOpen', {
            textDocument: { uri: untitled, languageId: 'carven', version: 1, text: value },
        });
        assert.equal(
            Boolean((await client.diagnostics(untitled, 1)).length),
            value !== 'fn main() {}',
        );
        await client.send('textDocument/didClose', { textDocument: { uri: untitled } });
        assert.deepEqual(
            ((await client.receive()).params as PublishDiagnosticsParams).diagnostics,
            [],
        );
    }

    const libraryURI = 'file:///workspace/pkg/lib.cv';
    const appURI = 'file:///workspace/pkg/app.cv';

    const library = 'const { println("analyzer-only"); } fn answer() -> i32 => 42;';
    const app = 'import .lib using answer; fn main() { let result = answer(); println(result); }';
    await client.send('textDocument/didOpen', {
        textDocument: { uri: libraryURI, languageId: 'carven', version: 1, text: library },
    });
    assert.deepEqual(await client.diagnostics(libraryURI, 1), []);
    await client.send('textDocument/didOpen', {
        textDocument: { uri: appURI, languageId: 'carven', version: 1, text: app },
    });
    assert.deepEqual(await client.diagnostics(libraryURI, 1), []);
    assert.deepEqual(await client.diagnostics(appURI, 1), []);
    await hover(client, appURI, app, 'result', 8, 'i32');
    await client.send('textDocument/didChange', {
        textDocument: { uri: libraryURI, version: 2 },
        contentChanges: [{ text: library.replaceAll('i32', 'i64') }],
    });
    assert.deepEqual(await client.diagnostics(libraryURI, 2), []);
    assert.deepEqual(await client.diagnostics(appURI, 1), []);
    await hover(client, appURI, app, 'result', 9, 'i64');

    await client.finish();
} finally {
    await client.close();
}

console.log(
    'LSP protocol: document updates, diagnostics, hover, Unicode, recovery and shutdown passed',
);

await temporary('scheduling', async (root) => {
    const analyzer = fileURLToPath(new URL('./gated-analyzer.ts', import.meta.url));
    client = new Client([process.execPath, analyzer], {
        ...process.env,
        CARVEN_ANALYZER_GATE: root,
    });
    const uri = 'file:///workspace/scheduling.cv';

    try {
        await client.initialize();
        await client.send('textDocument/didOpen', { textDocument: { uri, version: 1, text: 'x' } });
        await until(
            () => existsSync(join(root, 'blocked')),
            'analyzer never received first update',
        );

        const change = (version: number) =>
            client.send('textDocument/didChange', {
                textDocument: { uri, version },
                contentChanges: [{ text: 'x' }],
            });

        const query = (id: string) =>
            client.send(
                'textDocument/hover',
                { textDocument: { uri }, position: { line: 0, character: 0 } },
                id,
            );

        await change(2);
        await change(3);

        await query('cancelled');
        await query('before-change');

        await change(4);
        await query('after-change');
        await client.send('$/cancelRequest', { id: 'cancelled' });
        const cancelled = await client.receive();
        assert.equal(cancelled.id, 'cancelled');
        assert.equal(cancelled.error?.code, -32800);

        await change(5);
        await query('obsolete');
        await client.send('$/cancelRequest', { id: 'obsolete' });
        assert.equal((await client.receive()).error?.code, -32800);

        await change(6);
        await client.send('textDocument/documentSymbol', { textDocument: { uri } }, 'outline');
        await client.send('workspace/didChangeWatchedFiles', {
            changes: [{ uri: 'file:///workspace/helper.cv', type: 2 }],
        });
        await client.send('unknown', {}, 'responsive');
        assert.equal((await client.receive()).id, 'responsive');

        writeFileSync(join(root, 'release'), '');

        const replies = new Map<string | number, import('./client.ts').WireMessage>();
        const diagnostics: PublishDiagnosticsParams[] = [];
        while (replies.size !== 3 || !diagnostics.length) {
            const value = await client.receive();
            if (value.id !== undefined && value.id !== null) {
                replies.set(value.id, value);
            } else {
                diagnostics.push(value.params as PublishDiagnosticsParams);
            }
        }

        assert.equal(hoverText(replies.get('before-change')?.result as Hover), 'v3');
        assert.equal(hoverText(replies.get('after-change')?.result as Hover), 'v4');
        assert.equal(replies.get('outline')?.error, undefined);
        assert.deepEqual(replies.get('outline')?.result, []);
        assert(diagnostics.every((item) => item.version === 6));

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'LSP scheduling: edits while blocked, cancellation, response ordering ' +
        'and stale diagnostics passed',
);
