import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, unlinkSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import * as lsp from 'vscode-languageserver-protocol';
import { Client, hoverText, locations, outlines } from './client.ts';
import { createProject, localToolchain, position, temporary, uri } from './fixture.ts';

await temporary('context', async (parent) => {
    const root = join(parent, 'project');
    mkdirSync(root);

    const fixture = createProject(root, localToolchain());
    const { source, library } = fixture;
    let text = fixture.text;
    mkdirSync(join(root, 'crafts'));
    const libraryText =
        '// 😀\r\nstruct Point { width: i32, height: i64 }\r\nclass Counter { value: i32, fn read(self) -> i32 => self.value; }\r\nenum Mode { Ready, Done }\r\nfn provide() -> i32 => 42; const broken = ; fn after() {}';
    writeFileSync(library, libraryText);
    writeFileSync(join(root, 'other.cv'), 'fn alternate() {}');

    const config = join(root, 'xmake.lua');
    const configured = readFileSync(config, 'utf8');
    const split = configured.indexOf('target("app")');
    const original = configured
        .slice(split)
        .replace(
            '"src/*.cv"',
            JSON.stringify(join(root, 'src/*.cv')) +
                ', ' +
                JSON.stringify(join(root, 'crafts/**.cv')),
        )
        .replace('"other.cv"', JSON.stringify(join(root, 'other.cv')));
    const included = join(parent, 'targets.lua');

    writeFileSync(included, original);
    writeFileSync(config, configured.slice(0, split) + `includes(${JSON.stringify(included)})\n`);

    let client = new Client();
    const diagnostics = new Map<string, lsp.Diagnostic[]>();
    const registrations = new Map<string, lsp.FileSystemWatcher[]>();
    let watches: lsp.FileSystemWatcher[] = [];
    const warnings: string[] = [];

    const observe = async (message: import('./client.ts').WireMessage) => {
        if (
            message.method === 'client/registerCapability' ||
            message.method === 'client/unregisterCapability'
        ) {
            if (message.method === 'client/registerCapability') {
                const params = message.params as lsp.RegistrationParams;
                for (const registration of params.registrations) {
                    registrations.set(
                        registration.id,
                        (
                            registration.registerOptions as lsp.DidChangeWatchedFilesRegistrationOptions
                        ).watchers,
                    );
                }
            } else {
                const params = message.params as lsp.UnregistrationParams;
                for (const registration of params.unregisterations) {
                    registrations.delete(registration.id);
                }
            }
            watches = [...registrations.values()].flat();
            assert(message.id !== undefined && message.id !== null);
            await client.reply(message.id);
        } else if (message.method === 'textDocument/publishDiagnostics') {
            const params = message.params as lsp.PublishDiagnosticsParams;
            diagnostics.set(params.uri, params.diagnostics);
        } else if (message.method === 'window/logMessage') {
            warnings.push((message.params as lsp.LogMessageParams).message);
        } else {
            assert.fail(JSON.stringify(message));
        }
    };
    client.observe = observe;

    const symbols = async (query = '') =>
        (await client.query(lsp.WorkspaceSymbolRequest.type, { query })) ?? [];

    const document = async (path: string) =>
        outlines(
            await client.query(lsp.DocumentSymbolRequest.type, {
                textDocument: { uri: uri(path) },
            }),
        );

    const changed = (path: string, type: lsp.FileChangeType = 2) =>
        client.send('workspace/didChangeWatchedFiles', { changes: [{ uri: uri(path), type }] });

    const definition = async () =>
        locations(
            await client.query(lsp.DefinitionRequest.type, {
                textDocument: { uri: uri(source) },
                position: position(text, text.lastIndexOf('provide')),
            }),
        );

    const hover = async () =>
        hoverText(
            await client.query(lsp.HoverRequest.type, {
                textDocument: { uri: uri(source) },
                position: position(text, text.indexOf('value =')),
            }),
        );

    const change = (version: number) =>
        client.send('textDocument/didChange', {
            textDocument: { uri: uri(source), version },
            contentChanges: [{ text }],
        });

    try {
        await client.initialize(
            uri(join(root, 'src')),
            {},
            {
                workspace: {
                    didChangeWatchedFiles: {
                        dynamicRegistration: true,
                        relativePatternSupport: true,
                    },
                },
                textDocument: { documentSymbol: { hierarchicalDocumentSymbolSupport: true } },
            },
        );

        const all = await symbols();
        for (const name of ['Point', 'Counter', 'Mode', 'provide', 'after', 'alternate']) {
            assert(all.some((symbol) => symbol.name === name));
        }
        assert(!all.some((symbol) => symbol.name === 'String'));
        assert.deepEqual(
            (await symbols('pOi')).map((symbol) => symbol.name),
            ['Point'],
        );
        assert.deepEqual(await symbols('unmatched_name'), []);

        const outline = await document(library);
        assert.deepEqual(
            outline.map((symbol) => symbol.name),
            ['Point', 'Counter', 'Mode', 'provide', 'after'],
        );
        assert.deepEqual(
            outline[0].children?.map((symbol) => symbol.name),
            ['width', 'height'],
        );
        assert.equal(outline[1].children?.[1].kind, 6);
        assert.equal(outline[2].children?.[0].kind, 22);

        const start = libraryText.indexOf('Point');
        assert.deepEqual(outline[0].selectionRange, {
            start: position(libraryText, start),
            end: position(libraryText, start + 5),
        });
        assert(
            watches.some(
                (w) => typeof w.globPattern !== 'string' && w.globPattern.baseUri === uri(root),
            ),
        );
        assert(
            watches.some(
                (w) =>
                    typeof w.globPattern !== 'string' &&
                    w.globPattern.baseUri === uri(parent) &&
                    w.globPattern.pattern === 'targets.lua',
            ),
        );

        await client.send('textDocument/didOpen', {
            textDocument: { uri: uri(source), version: 1, text },
        });
        assert.equal((await definition())[0].uri, uri(library));

        writeFileSync(included, 'target("app"');
        await changed(included);
        assert.deepEqual(await symbols('Point'), []);
        assert.deepEqual(await definition(), []);
        assert(
            watches.some(
                (w) => typeof w.globPattern !== 'string' && w.globPattern.baseUri === uri(root),
            ),
        );
        assert(
            watches.some(
                (w) =>
                    typeof w.globPattern !== 'string' &&
                    w.globPattern.baseUri === uri(parent) &&
                    w.globPattern.pattern === 'targets.lua',
            ),
        );
        assert(warnings.some((value) => value.includes('Could not query Carven project')));
        await client.send('textDocument/didSave', { textDocument: { uri: uri(source) } });
        assert.deepEqual(await symbols('Point'), []);

        writeFileSync(included, original);
        await changed(included);
        assert((await symbols('Point')).length);
        assert.equal((await definition())[0].uri, uri(library));
        warnings.length = 0;

        writeFileSync(
            included,
            original.replace(JSON.stringify(join(root, 'src/*.cv')), JSON.stringify(source)),
        );
        await changed(included);
        // The explicit entry still imports helper, so its dependency remains available.
        assert.equal((await symbols('Point'))[0].location.uri, uri(library));
        assert.equal((await definition())[0].uri, uri(library));
        assert.deepEqual(await document(library), outline);
        assert(diagnostics.get(uri(library))?.length);

        writeFileSync(included, original.replace('target("app")', 'target("renamed")'));
        await changed(included);
        assert.equal((await symbols('Point'))[0].location.uri, uri(library));
        assert.equal((await definition())[0].uri, uri(library));

        const dependency = join(root, 'crafts/utility/value.cv');
        mkdirSync(join(root, 'crafts/utility'));
        writeFileSync(dependency, 'export fn amount() -> i32 => 7;');
        await changed(dependency, 1);
        assert.equal((await symbols('amount'))[0].location.uri, uri(dependency));

        const diskText = text;
        text =
            'import utility::value using amount; fn sample() { let value = amount(); println(value); }';
        await change(2);
        assert.equal(await hover(), 'i32');
        writeFileSync(dependency, 'export fn amount() -> i64 => 7;');
        await changed(dependency);
        assert.equal((await document(dependency))[0].name, 'amount');
        assert.equal(await hover(), 'i64');

        unlinkSync(dependency);
        await changed(dependency, 3);
        assert.deepEqual(await symbols('amount'), []);
        assert.deepEqual(diagnostics.get(uri(dependency)), []);

        text = diskText;
        await change(3);
        await client.send('textDocument/didClose', { textDocument: { uri: uri(source) } });

        const inactive = original
            .replace('target("app")', 'target("app")\n    set_default(false)')
            .replace(
                'target("other")\n    set_default(false)',
                'target("other")\n    set_default(true)',
            );

        writeFileSync(included, inactive);
        await changed(included);
        assert((await symbols('Point')).length);
        assert.deepEqual(diagnostics.get(uri(library)), []);

        writeFileSync(included, original);
        await changed(included);
        assert((await symbols('Point')).length);
        assert(diagnostics.get(uri(library))?.length);

        await client.send('textDocument/didOpen', {
            textDocument: { uri: uri(source), version: 1, text },
        });

        writeFileSync(included, original.slice(original.indexOf('target("other")')));
        await changed(included);
        assert.deepEqual(await symbols('Point'), []);
        assert.deepEqual(await definition(), []);
        assert.deepEqual(
            (await document(source)).map((symbol) => symbol.name),
            ['sample'],
        );

        writeFileSync(included, '');
        await changed(included);
        assert.deepEqual(await symbols('Point'), []);
        assert.deepEqual(
            (await document(source)).map((symbol) => symbol.name),
            ['sample'],
        );

        writeFileSync(included, original);
        await changed(included);
        assert((await symbols('Point')).length);
        assert.equal((await definition())[0].uri, uri(library));
        assert.equal(warnings.length, 0);

        await client.finish();

        await client.close();

        client = new Client();
        client.observe = observe;
        warnings.length = 0;
        await client.initialize(uri(join(root, 'src')), { target: 'app' });
        assert((await symbols('Point')).length);

        writeFileSync(included, original.replace('target("app")', 'target("renamed")'));
        await changed(included);
        assert.deepEqual(await symbols('Point'), []);
        assert(warnings.some((warning) => warning.includes('Carven target not found: app')));

        writeFileSync(included, original);
        await changed(included);
        assert((await symbols('Point')).length);

        await client.finish();

        await client.close();

        client = new Client();
        client.observe = observe;

        const validConfig = readFileSync(config, 'utf8');
        writeFileSync(config, 'includes(');
        warnings.length = 0;
        registrations.clear();
        watches = [];
        await client.initialize(
            uri(join(root, 'src')),
            {},
            {
                workspace: {
                    didChangeWatchedFiles: {
                        dynamicRegistration: true,
                        relativePatternSupport: true,
                    },
                },
            },
        );
        assert.deepEqual(await symbols('Point'), []);
        assert(
            watches.some(
                (w) => typeof w.globPattern !== 'string' && w.globPattern.baseUri === uri(root),
            ),
        );

        writeFileSync(config, validConfig);
        await changed(config);
        assert((await symbols('Point')).length);

        unlinkSync(config);
        await changed(config, 3);
        assert.deepEqual(await symbols('Point'), []);
        assert(
            watches.some(
                (w) =>
                    typeof w.globPattern !== 'string' &&
                    w.globPattern.baseUri === uri(root) &&
                    w.globPattern.pattern === 'xmake.lua',
            ),
        );

        writeFileSync(config, validConfig);
        await changed(config, 1);
        assert((await symbols('Point')).length);

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Context: unopened/recovered symbols, external watches, ' +
        'target/input/Craft refresh and removed diagnostics passed',
);
