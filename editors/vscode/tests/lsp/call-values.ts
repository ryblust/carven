import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { join } from 'node:path';
import * as lsp from 'vscode-languageserver-protocol';
import { Client } from './client.ts';
import { createProject, localToolchain, position, temporary, uri } from './fixture.ts';

await temporary('call-values', async (root) => {
    const { source, library } = createProject(root, localToolchain());
    const libraryText =
        'fn provide() -> i32 => 1;\nfn other() -> i32 => 2;\n' +
        'fn factory() => []() -> i32 { return provide(); };\n' +
        'fn invoke(callback: fn() -> i32) -> i32 => callback();';
    writeFileSync(library, libraryText);

    const unopened = join(root, 'src/unopened.cv');
    writeFileSync(
        unopened,
        'import .helper using factory; fn unopened() { let value = factory(); value(); }',
    );

    const text = `import .helper using { provide, other, factory, invoke }
fn sample() {
    let broken = ;
    let known = provide;
    let alias = known;
    let view: fn() -> i32 = alias;
    var dynamic: fn() -> i32 = known;
    dynamic = other;
    known(); alias(); view(); dynamic();
    let closure = []() -> i32 { let broken = ; return provide(); };
    let copy = closure; copy(); let closureView: fn() -> i32 = copy; closureView();
    let outer = [copy]() -> i32 {
        let nested = [copy]() -> i32 => copy();
        return nested();
    };
    outer();
    factory()();
    ([]() -> i32 => provide())();
    invoke(view);
}`;
    writeFileSync(source, text);

    const client = new Client();
    const diagnostics: string[] = [];
    client.observe = (message) => {
        assert.equal(message.method, 'textDocument/publishDiagnostics');
        const params = message.params as lsp.PublishDiagnosticsParams;
        diagnostics.push(
            ...params.diagnostics.map((item) =>
                typeof item.message === 'string' ? item.message : item.message.value,
            ),
        );
    };

    const prepare = (offset: number) =>
        client.query(lsp.CallHierarchyPrepareRequest.type, {
            textDocument: { uri: uri(source) },
            position: position(text, offset),
        });

    const outgoing = (item: lsp.CallHierarchyItem) =>
        client.query(lsp.CallHierarchyOutgoingCallsRequest.type, { item });

    const incoming = (item: lsp.CallHierarchyItem) =>
        client.query(lsp.CallHierarchyIncomingCallsRequest.type, { item });

    try {
        await client.initialize(uri(root));
        await client.send('textDocument/didOpen', {
            textDocument: { uri: uri(source), version: 1, text },
        });

        for (const spelling of [
            'known(); alias',
            'alias(); view',
            'view(); dynamic',
            'view: fn()',
        ]) {
            const items = await prepare(text.indexOf(spelling));
            assert(items);
            assert.equal(items[0].name, 'provide');
        }
        assert.equal(await prepare(text.indexOf('dynamic();')), null);

        const selected = await prepare(text.indexOf('sample'));
        assert(selected);
        const edges = await outgoing(selected[0]);
        assert(edges);
        assert.deepEqual(
            edges
                .filter((edge) => edge.to.name !== 'closure')
                .map((edge) => edge.to.name)
                .sort(),
            ['factory', 'invoke', 'provide'],
        );
        assert.equal(edges.find((edge) => edge.to.name === 'provide')?.fromRanges.length, 3);

        const closures = edges.filter((edge) => edge.to.name === 'closure');
        assert.equal(closures.length, 4, JSON.stringify({ edges, diagnostics }));

        const localClosure = closures.find(
            (edge) => edge.to.uri === uri(source) && edge.to.selectionRange.start.line === 9,
        );
        assert(localClosure);
        const body = await outgoing(localClosure.to);
        assert(body);
        assert.equal(body.length, 1);
        assert.equal(body[0].to.name, 'provide');

        const outer = closures.find(
            (edge) => edge.to.uri === uri(source) && edge.to.selectionRange.start.line === 11,
        );
        assert(outer);
        const nested = await outgoing(outer.to);
        assert(nested);
        assert.equal(nested.length, 1);
        assert.equal(nested[0].to.name, 'closure');

        const nestedBody = await outgoing(nested[0].to);
        assert(nestedBody);
        assert.equal(nestedBody.length, 1);
        assert.equal(nestedBody[0].to.name, 'closure');
        assert.deepEqual(nestedBody[0].to.selectionRange, localClosure.to.selectionRange);

        const fromNested = await incoming(nested[0].to);
        assert(fromNested);
        assert.equal(fromNested.length, 1);
        assert.deepEqual(fromNested[0].from.selectionRange, outer.to.selectionRange);

        const returned = closures.find((edge) => edge.to.uri === uri(library));
        assert(returned);
        const returnedBody = await outgoing(returned.to);
        assert(returnedBody);
        assert.equal(returnedBody[0].to.name, 'provide');

        const returnedCallers = await incoming(returned.to);
        assert(returnedCallers);
        assert.deepEqual(returnedCallers.map((edge) => edge.from.name).sort(), [
            'sample',
            'unopened',
        ]);

        const invoke = edges.find((edge) => edge.to.name === 'invoke');
        assert(invoke);
        assert.deepEqual(await outgoing(invoke.to), []);

        const direct = await prepare(text.indexOf('[]() -> i32 {'));
        assert(direct);
        assert.equal(direct[0].name, 'closure');

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Call values: concrete closures, immutable views, copied/captured/returned targets, ' +
        'nested ownership and unknown views passed',
);
