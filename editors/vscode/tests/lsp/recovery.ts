import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { join } from 'node:path';
import * as lsp from 'vscode-languageserver-protocol';
import { Client, completions, hoverText, locations } from './client.ts';
import { createProject, localToolchain, position, temporary, uri } from './fixture.ts';

await temporary('recovery', async (root) => {
    const { source, library } = createProject(root, localToolchain());
    writeFileSync(
        library,
        'fn damaged() { let note = "unfinished\n}\nstruct Point { width: i32 } fn provide() -> i64 => 42; fn unrelated() -> i32 => true;',
    );
    let text =
        'import .helper using { Point, provide } fn sample() { let _ = "😀"; let wrong: i32 = provide(); let broken = missing(); let syntax = ; let separator = 1\nlet good = provide(); println(good); let p = Point { width: 1 }; p.wid';
    writeFileSync(source, text);

    const consumer = join(root, 'src/consumer.cv');
    writeFileSync(consumer, 'import .helper using provide; fn use() -> i64 => provide();');

    const client = new Client();
    let sawError = false;
    const lexicalVersions = new Set<number | undefined>();
    client.observe = (message) => {
        assert.equal(message.method, 'textDocument/publishDiagnostics');
        const params = message.params as lsp.PublishDiagnosticsParams;
        sawError ||= params.diagnostics.some((item) => item.severity === 1);
        if (
            params.diagnostics.some(
                (item) =>
                    typeof item.message === 'string' && item.message.includes('string literal'),
            )
        ) {
            lexicalVersions.add(params.version);
        }
    };

    const at = (offset: number) => ({
        textDocument: { uri: uri(source) },
        position: position(text, offset),
    });

    const check = async (definitionAt: number, completionAt: number, references = false) => {
        assert.equal(
            locations(await client.query(lsp.DefinitionRequest.type, at(definitionAt)))[0].uri,
            uri(library),
        );
        assert.equal(
            hoverText(await client.query(lsp.HoverRequest.type, at(text.indexOf('good);')))),
            'i64',
        );
        if (references) {
            assert(
                locations(
                    await client.query(lsp.ReferencesRequest.type, {
                        ...at(definitionAt),
                        context: { includeDeclaration: false },
                    }),
                ).some((item) => item.uri === uri(consumer)),
            );
        }
        assert.deepEqual(
            completions(await client.query(lsp.CompletionRequest.type, at(completionAt))).map(
                (item) => [item.label, item.detail],
            ),
            [['width', 'i32']],
        );
        assert(sawError, 'queries retain original source errors');
    };

    const change = async (value: string, version: number) => {
        const previous = text;
        text = value;
        sawError = false;
        await client.send('textDocument/didChange', {
            textDocument: { uri: uri(source), version },
            contentChanges: [
                {
                    range: {
                        start: position(previous, 0),
                        end: position(previous, previous.length),
                    },
                    text,
                },
            ],
        });
    };

    try {
        await client.initialize(uri(root));
        await client.send('textDocument/didOpen', {
            textDocument: { uri: uri(source), version: 1, text },
        });
        await check(text.indexOf('provide();'), text.length, true);

        await change(
            'import .\nimport .helper using { Point, provide }\nfn sample() { let _ = "😀";\nlet call = provide(\nlet list = [1,\nlet index = [1][\nlet good = provide(); println(good); let p = Point { width: 7 }; p.wid\n}\nfn later() -> i64 => provide();',
            2,
        );
        await check(text.lastIndexOf('provide();'), text.indexOf('p.wid') + 5);

        await change(
            'import .helper using { Point, provide }\nfn sample() {\nlet note = "unfinished😀\n/* unfinished { [\nlet good = provide(); println(good); let p = Point { width: 7 }; p.wid;\n}\nlet banner = """\nunfinished text ( {\nfn later() -> i64 => provide();',
            3,
        );
        await check(text.lastIndexOf('provide();'), text.indexOf('p.wid') + 5, true);
        assert(lexicalVersions.has(3));

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Recovery: lexical/syntax errors, incremental edits, unopened definitions, ' +
        'references and completion passed',
);

await temporary('validation-facts', async (root) => {
    const { source, library } = createProject(root, localToolchain());
    writeFileSync(library, 'struct Point { width: i32 } fn provide() -> i64 => 42;');

    const consumer = join(root, 'src/consumer.cv');
    writeFileSync(consumer, 'import .helper using provide; fn use() -> i64 => provide();');

    const broken =
        'import .helper using { Point, provide } fn sample() -> i64 { var point = Point { width: 7 }; let moved = &&point; let bad = point.width; let good = provide(); return good; }';
    writeFileSync(source, broken);

    const client = new Client();
    const published = new Map<number | undefined, lsp.Diagnostic[]>();
    client.observe = (message) => {
        assert.equal(message.method, 'textDocument/publishDiagnostics');
        const params = message.params as lsp.PublishDiagnosticsParams;
        if (params.uri === uri(source)) published.set(params.version, params.diagnostics);
    };

    try {
        await client.initialize(uri(root));

        const repaired = broken.replace('bad = point.width', 'bad = moved.width');
        const partial = broken.replace(
            'let bad = point.width;',
            'let bad: bool = provide(); let missing = absent();',
        );

        for (const [index, text] of [broken, repaired, broken, partial, repaired].entries()) {
            const version = index + 1;
            if (version === 1) {
                await client.send('textDocument/didOpen', {
                    textDocument: { uri: uri(source), languageId: 'carven', version, text },
                });
            } else {
                await client.send('textDocument/didChange', {
                    textDocument: { uri: uri(source), version },
                    contentChanges: [{ text }],
                });
            }

            const at = (needle: string) => ({
                textDocument: { uri: uri(source) },
                position: position(text, text.indexOf(needle)),
            });
            assert.equal(
                locations(await client.query(lsp.DefinitionRequest.type, at('provide();')))[0].uri,
                uri(library),
            );
            assert.equal(hoverText(await client.query(lsp.HoverRequest.type, at('good;'))), 'i64');
            assert(
                locations(
                    await client.query(lsp.ReferencesRequest.type, {
                        ...at('provide();'),
                        context: { includeDeclaration: false },
                    }),
                ).some((item) => item.uri === uri(consumer)),
            );
            assert(published.has(version), 'diagnostics describe the edited version');
            assert.equal(
                published.get(version)!.some((item) => item.code === 'CV-ACCESS-UNAVAILABLE'),
                version === 1 || version === 3,
            );

            if (version === 4) {
                assert(published.get(version)!.some((item) => item.code === 'CV-TYPE-MISMATCH'));
                assert.equal(
                    locations(await client.query(lsp.DefinitionRequest.type, at('provide();')))[0]
                        .uri,
                    uri(library),
                    'the callee in a failed initializer survives recovery',
                );
            }
            if (version === 2 || version === 5) {
                assert(!published.get(version)!.some((item) => item.severity === 1));
            }
        }

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Recovery: partial construction and ownership failures retain cross-file facts through repair',
);
