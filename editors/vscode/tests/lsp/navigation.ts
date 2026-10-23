import assert from 'node:assert/strict';
import { isDeepStrictEqual } from 'node:util';
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import * as lsp from 'vscode-languageserver-protocol';
import { Client, completions, locations } from './client.ts';
import { createProject, localToolchain, position, temporary, uri } from './fixture.ts';

await temporary('navigation', async (root) => {
    const { source, library } = createProject(root, localToolchain());
    const libraryText =
        'struct Point { width: i32, height: i64 }\nfn provide() -> i64 => 42;\nprivate fn hidden() {}\nenum Mode { Ready, Done }\nclass Counter { value: i32, fn new(value: i32) -> Counter => { value: value }; fn read(self) -> i32 => self.value; private fn secret(self) -> i32 => self.value; }';
    writeFileSync(library, libraryText);

    let text =
        'import .helper using { provide, Point }\r\nfn sample() -> i64 { let _ = "😀"; let point = Point { width: 1, height: 2 }; println(point.width); return provide(); }';
    writeFileSync(source, text);

    const consumer = join(root, 'src/consumer.cv');
    let consumerText = 'import .helper using provide; fn consumer() -> i64 => provide();';
    writeFileSync(consumer, consumerText);

    const client = new Client();
    let version = 1;

    const at = (offset: number) => ({
        textDocument: { uri: uri(source) },
        position: position(text, offset),
    });

    const completion = async (offset = text.length) =>
        completions(await client.query(lsp.CompletionRequest.type, at(offset)));

    const references = async (includeDeclaration = false) =>
        locations(
            await client.query(lsp.ReferencesRequest.type, {
                ...at(text.lastIndexOf('provide')),
                context: { includeDeclaration },
            }),
        );

    const change = async (value: string) => {
        text = value;
        await client.send('textDocument/didChange', {
            textDocument: { uri: uri(source), version: ++version },
            contentChanges: [{ text }],
        });
    };

    const diskChanged = () =>
        client.send('workspace/didChangeWatchedFiles', {
            changes: [{ uri: uri(consumer), type: 2 }],
        });

    try {
        await client.initialize(uri(root));
        await client.send('textDocument/didOpen', {
            textDocument: { uri: uri(source), version, text },
        });

        const definition = locations(
            await client.query(lsp.DefinitionRequest.type, at(text.lastIndexOf('provide'))),
        );
        const start = libraryText.indexOf('provide');
        assert.deepEqual(definition, [
            {
                uri: uri(library),
                range: {
                    start: position(libraryText, start),
                    end: position(libraryText, start + 7),
                },
            },
        ]);

        const refs = await references();
        assert.equal(refs.length, 4);
        assert.deepEqual(
            new Set(refs.map((item) => item.uri)),
            new Set([uri(source), uri(consumer)]),
        );

        const declarations = await references(true);
        assert.equal(declarations.length, 5);
        assert(declarations.some((item) => item.uri === uri(library)));

        writeFileSync(consumer, consumerText.replace('provide();', 'provide() + provide();'));
        await diskChanged();
        assert.equal((await references()).length, 5);

        consumerText =
            '//' +
            'x'.repeat(500000) +
            '\n' +
            consumerText.replace('provide();', Array(40).fill('provide()').join(' + ') + ';');
        writeFileSync(consumer, consumerText);
        await diskChanged();
        const shared = await references();
        assert.equal(shared.length, 43);
        const last = consumerText.lastIndexOf('provide');
        assert(
            shared.some(
                (item) =>
                    item.uri === uri(consumer) &&
                    isDeepStrictEqual(item.range, {
                        start: position(consumerText, last),
                        end: position(consumerText, last + 7),
                    }),
            ),
        );
        assert.equal(
            locations(
                await client.query(lsp.DefinitionRequest.type, at(text.indexOf('width);'))),
            )[0].uri,
            uri(library),
        );

        await change(
            'import .helper using { provide as compute, Point }\r\nfn sample(input: i32) { let _ = "😀"; let earlier = 1; if true { let scoped = 2; println(scoped); } comp',
        );
        const items = Object.fromEntries((await completion()).map((item) => [item.label, item]));
        for (const label of ['compute', 'Point', 'input', 'earlier']) {
            assert(items[label]);
        }
        for (const label of ['hidden', 'scoped']) {
            assert(!items[label]);
        }
        for (const label of ['provide', 'consumer']) {
            assert(items[label].additionalTextEdits?.length);
        }
        assert.equal(items.input.detail, 'i32');
        assert.deepEqual(items.compute.textEdit, {
            newText: 'compute',
            range: { start: position(text, text.length - 4), end: position(text, text.length) },
        });

        await change(
            'import .helper using Point; fn sample() { let point = Point { width: 1, height: 2 }; point.wid',
        );
        const members = Object.fromEntries((await completion()).map((item) => [item.label, item]));
        assert.deepEqual(new Set(Object.keys(members)), new Set(['width', 'height']));
        assert.equal(members.width.kind, 5);
        assert.equal(members.height.detail, 'i64');
        assert.equal(
            locations(await client.query(lsp.DefinitionRequest.type, at(text.indexOf('Point;'))))[0]
                .uri,
            uri(library),
        );

        await change('import .helper using Point; fn sample(input: Point) {}');
        const types = new Set(
            (await completion(text.lastIndexOf('Point') + 2)).map((item) => item.label),
        );
        for (const label of ['Point', 'i32', 'String']) {
            assert(types.has(label));
        }
        assert(!types.has('println'));

        await change('fn sample() { let text = "value"; text.by');
        const methods = new Set((await completion()).map((item) => item.label));
        for (const label of ['bytes', 'chars', 'byte_length', 'is_empty']) {
            assert(methods.has(label));
        }

        await change('import .helper using Mode; fn sample() { let value = Mode::Re');
        assert.deepEqual(
            new Set((await completion()).map((item) => item.label)),
            new Set(['Ready', 'Done']),
        );

        await change(
            'import .helper using Counter; fn sample() { let counter = Counter::new(1); counter.rea(); }',
        );
        assert.deepEqual(
            (await completion(text.indexOf('rea()') + 3)).map((item) => item.label),
            ['read'],
        );

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Navigation: unopened definitions, reverse references, declarations, ' +
        'lexical and imported member completion passed',
);

await temporary('standard-navigation', async (root) => {
    const source = uri(join(root, 'main.cv'));
    const text =
        'import std::json using JSONValue;\n' +
        'let document = JSONValue::parse("{}")?;\nprintln(document);';
    const client = new Client();
    const diagnostics = new Map<string, lsp.PublishDiagnosticsParams>();
    client.observe = (message) => {
        assert.equal(message.method, 'textDocument/publishDiagnostics', JSON.stringify(message));
        const params = message.params as lsp.PublishDiagnosticsParams;
        diagnostics.set(params.uri, params);
    };
    const definition = async () => {
        const result = locations(
            await client.query(lsp.DefinitionRequest.type, {
                textDocument: { uri: source },
                position: position(text, text.indexOf('parse(')),
            }),
        );
        assert.equal(result.length, 1);
        return result[0];
    };

    try {
        await client.initialize(uri(root));
        await client.send('textDocument/didOpen', {
            textDocument: { uri: source, version: 1, text },
        });
        const original = await definition();
        const standard = original.uri;
        const diskText = readFileSync(new URL(standard), 'utf8');
        await client.send('textDocument/didOpen', {
            textDocument: { uri: standard, version: 1, text: diskText },
        });
        assert.deepEqual(await definition(), original);
        assert.equal(diagnostics.get(standard)?.version, 1);
        const tokens = await client.query(lsp.SemanticTokensRequest.type, {
            textDocument: { uri: standard },
        });
        assert(tokens?.data.length);
        assert.deepEqual(
            await client.query(lsp.WorkspaceSymbolRequest.type, { query: 'JSONValue' }),
            [],
        );

        await client.send('textDocument/didChange', {
            textDocument: { uri: standard, version: 2 },
            contentChanges: [{ text: '// editor overlay\n' + diskText }],
        });
        const shifted = await definition();
        assert.equal(shifted.uri, standard);
        assert.equal(shifted.range.start.line, original.range.start.line + 1);
        assert.equal(diagnostics.get(standard)?.version, 2);

        await client.send('textDocument/didClose', { textDocument: { uri: standard } });
        assert.deepEqual(await definition(), original);
        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Standard navigation: opened dependency overlays preserve module identity and close restores disk',
);
