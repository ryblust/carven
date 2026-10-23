import assert from 'node:assert/strict';
import * as lsp from 'vscode-languageserver-protocol';
import { Client, hoverText } from './client.ts';
import { position } from './fixture.ts';

const client = new Client();
const uri = 'file:///workspace/native.cv';
const text = `import "native.hpp" using vendor::{compute, Widget}
import "left.hpp" using left::*;
import "right.hpp" using right::*;
private import(cpp) fn declared(value: i32) -> i32 = ::vendor::provider;
#[cpp] ---
int hidden_cpp_symbol() { return 1; }
---
fn sample(value: Widget) {
    let broken = ;
    compute(1);
    ::vendor::global_call();
    mystery();
    declared(1);
}`;

const at = (offset: number) => ({ textDocument: { uri }, position: position(text, offset) });

const hover = async (offset: number) =>
    hoverText(await client.query(lsp.HoverRequest.type, at(offset)));

try {
    await client.initialize();
    await client.send('textDocument/didOpen', { textDocument: { uri, version: 1, text } });

    const selected = await hover(text.lastIndexOf('compute'));
    assert(selected.startsWith('::vendor::compute\n\n'), selected);
    assert(
        selected.includes('Import: import "native.hpp" using vendor::{compute, Widget}'),
        selected,
    );
    assert(selected.includes('C++ compiler'));

    const global = await hover(text.indexOf('global_call'));
    assert(global.startsWith('::vendor::global_call\n\n'));
    assert(!global.includes('Import:'));

    const open = await hover(text.indexOf('mystery'));
    assert(open.startsWith('mystery\n\n'));
    assert(open.includes('Unqualified C++ lookup'));
    assert(open.includes('left::*') && open.includes('right::*'));
    assert(!open.includes('::left::mystery'));

    const declared = await hover(text.lastIndexOf('declared'));
    assert(declared.startsWith('fn declared(value: i32) -> i32\n\n'), declared);
    assert(declared.includes('C++ provider: ::vendor::provider'));

    const provider = await hover(text.indexOf('provider'));
    assert(provider.startsWith('::vendor::provider\n\n'));

    assert(
        (await hover(text.indexOf('native.hpp'))).includes('declarations are not parsed by Carven'),
    );
    assert((await hover(text.indexOf('#[cpp]'))).includes('preserved verbatim'));
    assert.equal(
        await client.query(lsp.HoverRequest.type, at(text.indexOf('hidden_cpp_symbol'))),
        null,
    );
    assert.equal(
        await client.query(lsp.CallHierarchyPrepareRequest.type, at(text.lastIndexOf('compute'))),
        null,
    );

    await client.finish();
} finally {
    await client.close();
}
console.log(
    'Interop hover: exact C++ paths/imports, namespace lookup, declared providers ' +
        'and opaque fragment boundaries passed',
);
