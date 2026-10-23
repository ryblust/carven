import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { join } from 'node:path';
import * as lsp from 'vscode-languageserver-protocol';
import { Client } from './client.ts';
import { createProject, localToolchain, position, temporary, uri } from './fixture.ts';

await temporary('calls', async (root) => {
    const { source, library } = createProject(root, localToolchain());
    const libraryText =
        'fn leaf() -> i32 => 1;\r\nfn provide() -> i32 { let _ = "😀"; leaf(); return leaf(); }\r\n' +
        'class Counter { value: i32, fn new(value: i32) -> Counter => { value: value }; fn read(self) -> i32 => leaf(); }';
    writeFileSync(library, libraryText);

    const consumer = join(root, 'src/consumer.cv');
    const consumerText =
        'import .helper using provide;\r\nfn consume() -> i32 { let _ = "😀"; provide(); return provide(); }\r\nfn reference() { let unused = provide; }';
    writeFileSync(consumer, consumerText);
    writeFileSync(
        join(root, 'src/unrelated.cv'),
        'fn provide() -> i32 => 2; fn unrelated() -> i32 => provide();',
    );

    let text =
        '// 😀 calls\r\nimport .helper using { provide as compute, Counter }\r\nfn sample() {\r\n    let note = "unfinished\r\n    let broken = unknown();\r\n    let callback = compute;\r\n    callback();\r\n    let counter = Counter::new(1); counter.read();\r\n    compute();\r\n    compute(';
    writeFileSync(source, text);

    const client = new Client();

    const prepare = (offset: number) =>
        client.query(lsp.CallHierarchyPrepareRequest.type, {
            textDocument: { uri: uri(source) },
            position: position(text, offset),
        });

    const incoming = (item: lsp.CallHierarchyItem) =>
        client.query(lsp.CallHierarchyIncomingCallsRequest.type, { item });

    const outgoing = (item: lsp.CallHierarchyItem) =>
        client.query(lsp.CallHierarchyOutgoingCallsRequest.type, { item });

    try {
        await client.initialize(uri(root));
        await client.send('textDocument/didOpen', {
            textDocument: { uri: uri(source), version: 1, text },
        });

        const prepared = await prepare(text.lastIndexOf('compute'));
        assert(prepared);
        assert.equal(prepared.length, 1);
        const provided = prepared[0];
        assert.equal(provided.name, 'provide');
        assert.equal(provided.uri, uri(library));
        assert.equal(provided.kind, lsp.SymbolKind.Function);
        assert.equal(provided.detail, 'fn provide() -> i32');
        assert.deepEqual(
            provided.selectionRange.start,
            position(libraryText, libraryText.indexOf('provide')),
        );

        const callers = await incoming(provided);
        assert(callers);
        assert.deepEqual(callers.map((call) => call.from.name).sort(), ['consume', 'sample']);

        const sample = callers.find((call) => call.from.name === 'sample');
        assert(sample);
        assert.equal(sample.from.uri, uri(source));
        assert.deepEqual(
            sample.fromRanges.map((range) => range.start),
            [
                position(text, text.indexOf('callback();')),
                position(text, text.indexOf('compute();')),
                position(text, text.lastIndexOf('compute')),
            ],
        );

        const consume = callers.find((call) => call.from.name === 'consume');
        assert(consume);
        assert.equal(consume.from.uri, uri(consumer));
        assert.deepEqual(
            consume.fromRanges.map((range) => range.start),
            [...consumerText.matchAll(/provide\(/g)].map((match) =>
                position(consumerText, match.index),
            ),
        );

        const targets = await outgoing(provided);
        assert(targets);
        assert.equal(targets.length, 1);
        assert.equal(targets[0].to.name, 'leaf');
        assert.equal(targets[0].to.uri, uri(library));
        assert.deepEqual(
            targets[0].fromRanges.map((range) => range.start),
            [...libraryText.matchAll(/leaf\(\)/g)]
                .slice(1, 3)
                .map((match) => position(libraryText, match.index)),
        );

        const reverse = await outgoing(consume.from);
        assert(reverse);
        assert.equal(reverse.length, 1);
        assert.equal(reverse[0].to.name, 'provide');
        assert.equal(reverse[0].fromRanges.length, 2);

        const sampleTargets = await outgoing(sample.from);
        assert(sampleTargets);
        assert.deepEqual(sampleTargets.map((call) => call.to.name).sort(), [
            'new',
            'provide',
            'read',
        ]);
        assert(
            sampleTargets
                .filter((call) => call.to.name !== 'provide')
                .every((call) => call.to.kind === lsp.SymbolKind.Method),
        );

        const callback = await prepare(text.indexOf('callback();'));
        assert(callback);
        assert.equal(callback[0].name, 'provide');

        const selected = await prepare(text.indexOf('sample'));
        assert(selected);
        assert.equal(selected[0].name, 'sample');
        assert.deepEqual(await prepare(text.indexOf('unfinished')), null);

        text =
            'import .helper using provide as compute;\r\ncompute();\r\nfn unrelated() {}\r\nlet _ = "😀"; compute();';
        await client.send('textDocument/didChange', {
            textDocument: { uri: uri(source), version: 2 },
            contentChanges: [{ text }],
        });
        const stale = await client.response(lsp.CallHierarchyIncomingCallsRequest.method, {
            item: provided,
        });
        assert.equal(stale.error?.code, lsp.LSPErrorCodes.ContentModified);

        const fresh = await prepare(text.lastIndexOf('compute'));
        assert(fresh);
        const updated = await incoming(fresh[0]);
        assert(updated);
        assert.deepEqual(updated.map((call) => call.from.name).sort(), ['consume', 'main']);

        const entry = updated.find((call) => call.from.name === 'main');
        assert(entry);
        assert.equal(entry.fromRanges.length, 2);

        const entryCalls = await outgoing(entry.from);
        assert(entryCalls);
        assert.equal(entryCalls.length, 1);
        assert.equal(entryCalls[0].to.name, 'provide');
        assert.equal(entryCalls[0].fromRanges.length, 2);

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Calls: resolved aliases/methods, unopened callers and callees, grouped UTF-16 sites, ' +
        'editing recovery and fresh hierarchy passed',
);
