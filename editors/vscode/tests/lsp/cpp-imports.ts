import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import * as lsp from 'vscode-languageserver-protocol';
import { Client, completions, locations } from './client.ts';
import { createProject, localToolchain, position, temporary, uri } from './fixture.ts';

await temporary('cpp-imports', async (root) => {
    const { source, library } = createProject(root, localToolchain());
    const libraryText =
        'import "helper-native.hpp" using foreign::foreignOnly; fn provide() -> i32 => 1; fn compute() -> i32 => 2;';
    writeFileSync(library, libraryText);

    let text = `// 😀 explicit imports
import .helper using provide;
import "native.hpp" using vendor::{compute, Widget}
import "native.hpp" using vendor::compute;
import "namespace.hpp" using extra::*;
fn sample(value: Widget) {
    let broken = ;
    compute(1);
    value.opaque();
    Widget::opaque();
    ::vendor::compute();
    unknown();
    comp
}
fn nested(value: Widget::Nested) {}
fn global_type(value: ::Widget) {}
fn shadow(compute: i32) { let value = compute; com }
`;
    writeFileSync(source, text);

    const client = new Client();
    let version = 1;

    const at = (offset: number) => ({
        textDocument: { uri: uri(source) },
        position: position(text, offset),
    });

    const definitions = async (offset: number) =>
        locations(await client.query(lsp.DefinitionRequest.type, at(offset)));

    const complete = async (offset: number) =>
        completions(await client.query(lsp.CompletionRequest.type, at(offset)));

    const change = async (value: string) => {
        text = value;
        await client.send('textDocument/didChange', {
            textDocument: { uri: uri(source), version: ++version },
            contentChanges: [{ text }],
        });
    };

    const importRange = (name: string): lsp.Range => ({
        start: position(text, text.indexOf(name)),
        end: position(text, text.indexOf(name) + name.length),
    });

    try {
        await client.initialize(uri(root));
        await client.send('textDocument/didOpen', {
            textDocument: { uri: uri(source), version, text },
        });
        assert.deepEqual(await definitions(text.indexOf('compute(1)')), [
            { uri: uri(source), range: importRange('compute') },
        ]);

        for (const spelling of ['Widget)', 'Widget::opaque', 'Widget::Nested']) {
            assert.deepEqual(
                await definitions(text.indexOf(spelling)),
                [{ uri: uri(source), range: importRange('Widget') }],
                spelling,
            );
        }

        for (const spelling of ['opaque()', 'Nested)', 'unknown()', '::vendor::compute']) {
            const offset =
                text.indexOf(spelling) + (spelling.startsWith('::') ? '::vendor::'.length : 0);
            assert.deepEqual(await definitions(offset), []);
        }

        const offset = text.indexOf('comp\n') + 4;
        const names = await complete(offset);
        const compute = names.filter((item) => item.label === 'compute');
        assert.equal(compute.length, 1);
        assert.equal(compute[0].kind, lsp.CompletionItemKind.Text);
        assert.equal(compute[0].detail, 'C++ import ::vendor::compute');
        assert.deepEqual(compute[0].textEdit, {
            range: { start: position(text, offset - 4), end: position(text, offset) },
            newText: 'compute',
        });
        assert(!compute[0].additionalTextEdits);
        assert(
            !names.some((item) =>
                ['foreignOnly', 'extra', 'unknown', 'opaque'].includes(String(item.label)),
            ),
        );

        const types = await complete(text.indexOf('value: Widget') + 'value: Wid'.length);
        assert.equal(
            types.find((item) => item.label === 'Widget')?.kind,
            lsp.CompletionItemKind.Text,
        );
        assert.equal(
            types.find((item) => item.label === 'Widget')?.detail,
            'C++ import ::vendor::Widget',
        );
        assert.deepEqual(await complete(text.indexOf('::Widget') + '::Wid'.length), []);

        for (const prefix of ['value.opa', 'Widget::opa', 'Widget::Nes']) {
            assert.deepEqual(await complete(text.indexOf(prefix) + prefix.length), [], prefix);
        }

        const local = await complete(text.lastIndexOf('com }') + 3);
        assert.equal(
            local.find((item) => item.label === 'compute')?.kind,
            lsp.CompletionItemKind.Variable,
        );
        const parameter = text.lastIndexOf('compute: i32');
        assert.deepEqual(await definitions(text.lastIndexOf('compute;')), [
            {
                uri: uri(source),
                range: { start: position(text, parameter), end: position(text, parameter + 7) },
            },
        ]);

        await change(
            'import .helper using compute; import "native.hpp" using vendor::Widget; fn sample() -> i32 => compute();',
        );
        const declared = libraryText.indexOf('compute()');
        assert.deepEqual(await definitions(text.lastIndexOf('compute')), [
            {
                uri: uri(library),
                range: {
                    start: position(libraryText, declared),
                    end: position(libraryText, declared + 7),
                },
            },
        ]);
        assert.equal(
            (await complete(text.lastIndexOf('compute') + 3)).find(
                (item) => item.label === 'compute',
            )?.kind,
            lsp.CompletionItemKind.Function,
        );

        await change(
            'import "changed.hpp" using changed::{compute, Gadget} fn sample(value: Gadget) { compute(); comp',
        );
        const fresh = await complete(text.length);
        assert.equal(
            fresh.find((item) => item.label === 'compute')?.detail,
            'C++ import ::changed::compute',
        );
        assert(fresh.some((item) => item.label === 'Gadget'));
        assert(!fresh.some((item) => item.label === 'Widget'));
        assert.deepEqual(await definitions(text.lastIndexOf('compute')), [
            { uri: uri(source), range: importRange('compute') },
        ]);

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'C++ imports: explicit binding completion/navigation, type sites, module/local precedence, ' +
        'unknown members and incremental refresh passed',
);
