import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import * as lsp from 'vscode-languageserver-protocol';
import { Client } from './client.ts';
import { createProject, localToolchain, position, temporary, uri } from './fixture.ts';

await temporary('decorations', async (root) => {
    const { source, library } = createProject(root, localToolchain());
    writeFileSync(
        library,
        'struct Point { width: i32 } fn combine(left: i32, right: i64) -> i64 => right;',
    );
    const text =
        'import .helper using { combine as compute, Point }\r\nfn sample(input: i32) { let _ = "😀"; let broken = ; let count = compute(1, 2); var change = count; let named: i64 = count; let p = Point { width: 1 }; let width = p.width; compute(1, ';
    writeFileSync(source, text);

    const client = new Client();
    const refreshes = new Set<string>();
    client.observe = async (message) => {
        if (
            ['workspace/semanticTokens/refresh', 'workspace/inlayHint/refresh'].includes(
                message.method ?? '',
            )
        ) {
            refreshes.add(message.method!);
            assert(message.id !== undefined && message.id !== null);
            await client.reply(message.id);
        } else {
            assert.equal(
                message.method,
                'textDocument/publishDiagnostics',
                JSON.stringify(message),
            );
        }
    };

    const decode = (data: number[]) => {
        let line = 0;
        let character = 0;
        const output: {
            position: lsp.Position;
            length: number;
            kind: number;
            modifiers: number;
        }[] = [];
        for (let index = 0; index < data.length; index += 5) {
            const [dl, dc, length, kind, modifiers] = data.slice(index, index + 5);
            line += dl;
            character = dl ? dc : character + dc;
            output.push({ position: { line, character }, length, kind, modifiers });
        }
        return output;
    };

    try {
        await client.initialize(
            uri(root),
            {},
            {
                workspace: {
                    semanticTokens: { refreshSupport: true },
                    inlayHint: { refreshSupport: true },
                },
                textDocument: {
                    semanticTokens: {
                        requests: { full: true, range: true },
                        tokenTypes: [
                            'type',
                            'struct',
                            'class',
                            'enum',
                            'typeParameter',
                            'parameter',
                            'variable',
                            'property',
                            'enumMember',
                            'function',
                            'method',
                        ],
                        tokenModifiers: [
                            'declaration',
                            'readonly',
                            'static',
                            'async',
                            'defaultLibrary',
                        ],
                        formats: ['relative'],
                    },
                },
            },
        );
        await client.send('textDocument/didOpen', {
            textDocument: { uri: uri(source), version: 1, text },
        });

        const result = await client.query(lsp.SemanticTokensRequest.type, {
            textDocument: { uri: uri(source) },
        });
        assert(result);
        const tokens = decode(result.data);
        const token = (needle: string, kind: number) => {
            const at = position(text, text.indexOf(needle));
            const found = tokens.find(
                (item) =>
                    item.kind === kind &&
                    item.position.line === at.line &&
                    item.position.character === at.character,
            );
            assert(found);
            return found;
        };
        assert.equal(token('compute(1', 9).length, 7);
        assert(token('input:', 5).modifiers & 1);
        assert(token('count =', 6).modifiers & 2);
        assert(!(token('change =', 6).modifiers & 2));
        assert.equal(token('width;', 7).length, 5);

        const visible = { start: position(text, 0), end: position(text, text.length) };
        const hints = await client.query(lsp.InlayHintRequest.type, {
            textDocument: { uri: uri(source) },
            range: visible,
        });
        assert(hints);
        const count = position(text, text.indexOf('count =') + 5);
        const atCount = (hint: lsp.InlayHint) =>
            hint.position.line === count.line && hint.position.character === count.character;
        assert(hints.some((hint) => atCount(hint) && hint.label === ': i64' && hint.kind === 1));
        assert(
            hints.some((hint) => hint.label === 'left:' && hint.kind === 2 && hint.paddingRight),
        );

        const named = position(text, text.indexOf('named:') + 5);
        assert(
            !hints.some(
                (hint) =>
                    hint.position.line === named.line &&
                    hint.position.character === named.character,
            ),
        );

        const start = position(text, text.indexOf('width;'));
        const end = position(text, text.indexOf('width;') + 5);
        const limited = await client.query(lsp.SemanticTokensRangeRequest.type, {
            textDocument: { uri: uri(source) },
            range: { start, end },
        });
        assert(limited);
        assert.deepEqual(
            decode(limited.data).map((item) => [item.position, item.kind]),
            [[start, 7]],
        );

        writeFileSync(
            library,
            'struct Point { width: i64 } fn combine(left: i32, right: i64) -> i32 => left;',
        );
        await client.send('workspace/didChangeWatchedFiles', {
            changes: [{ uri: uri(library), type: 2 }],
        });
        const updated = await client.query(lsp.InlayHintRequest.type, {
            textDocument: { uri: uri(source) },
            range: visible,
        });
        assert(updated?.some((hint) => atCount(hint) && hint.label === ': i32'));
        assert(
            refreshes.has('workspace/semanticTokens/refresh') &&
                refreshes.has('workspace/inlayHint/refresh'),
        );

        await client.finish();
    } finally {
        await client.close();
    }
});

console.log(
    'Decorations: semantic roles/modifiers, UTF-16 full/range tokens, ' +
        'type/parameter hints and dependency refresh passed',
);
