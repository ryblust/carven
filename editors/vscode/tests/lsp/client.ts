import assert from 'node:assert/strict';
import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import { setTimeout as delay } from 'node:timers/promises';
import { StreamMessageReader, StreamMessageWriter } from 'vscode-languageserver-protocol/node';
import {
    CompletionList,
    DocumentSymbol,
    Location,
    TextDocumentEdit,
    TextEdit,
    MarkupContent,
    type ClientCapabilities,
    type InitializeParams,
    type InitializeResult,
    type PublishDiagnosticsParams,
    type ProtocolRequestType,
    type Message,
    type ResponseMessage,
    type NotificationMessage,
    type Hover,
    type CompletionItem,
    type CodeAction,
    type Command,
    type WorkspaceEdit,
} from 'vscode-languageserver-protocol';
import { position } from './fixture.ts';

// Raw envelopes are used only for framing/lifecycle assertions and server messages.
export type WireMessage = Message &
    Partial<Pick<ResponseMessage, 'id' | 'error' | 'result'>> &
    Partial<Pick<NotificationMessage, 'method' | 'params'>>;
export type Observer = (message: WireMessage) => void | Promise<void>;
export const diagnosticsOnly: Observer = (message) =>
    assert.equal(message.method, 'textDocument/publishDiagnostics', JSON.stringify(message));

export function frame(value: unknown): Buffer {
    const body = Buffer.from(JSON.stringify(value));
    return Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`), body]);
}

export class Client {
    readonly child: ChildProcessWithoutNullStreams;
    private reader: StreamMessageReader;
    private writer: StreamMessageWriter;
    private messages: WireMessage[] = [];
    private waiter?: { resolve: (message: WireMessage) => void; reject: (error: Error) => void };
    private failure?: Error;
    private stderr = '';
    private sequence = 100;
    private exited: Promise<number | null>;
    observe: Observer = diagnosticsOnly;

    constructor(analyzer: readonly string[] = [], env = process.env, cwd?: string) {
        assert(process.env.CARVEN_LSP, 'Set CARVEN_LSP');
        this.child = spawn(
            process.env.CARVEN_LSP,
            analyzer.length ? ['--analyzer', ...analyzer] : [],
            {
                env,
                cwd,
                stdio: 'pipe',
            },
        );
        this.child.stderr.setEncoding('utf8').on('data', (chunk) => (this.stderr += chunk));
        this.reader = new StreamMessageReader(this.child.stdout);
        this.writer = new StreamMessageWriter(this.child.stdin);

        const fail = (error: Error) => {
            this.failure = error;
            this.waiter?.reject(error);
            this.waiter = undefined;
        };
        this.reader.onError((error) => fail(error));
        this.child.on('error', fail);
        this.exited = new Promise((resolve) => this.child.on('exit', (code) => resolve(code)));
        this.child.stdout.on('end', () => fail(new Error(`LSP closed: ${this.stderr}`)));

        this.reader.listen((message) => {
            if (this.waiter) {
                this.waiter.resolve(message as WireMessage);
                this.waiter = undefined;
            } else {
                this.messages.push(message as WireMessage);
            }
        });
    }

    async send(
        method: string,
        params?: unknown,
        id?: number | string,
        fragmented = false,
    ): Promise<void> {
        const message = {
            jsonrpc: '2.0',
            method,
            ...(params === undefined ? {} : { params }),
            ...(id === undefined ? {} : { id }),
        };
        if (fragmented) {
            const bytes = frame(message);
            for (let index = 0; index < bytes.length; index += 7) {
                this.child.stdin.write(bytes.subarray(index, index + 7));
            }
        } else {
            await this.writer.write(message);
        }
    }

    async reply(id: string | number, result: ResponseMessage['result'] = null): Promise<void> {
        const response: ResponseMessage = { jsonrpc: '2.0', id, result };
        await this.writer.write(response);
    }

    async receive(): Promise<WireMessage> {
        const message = this.messages.shift();
        if (message) return message;
        if (this.failure) throw this.failure;

        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                this.waiter = undefined;
                reject(new Error(`LSP response timed out: ${this.stderr}`));
            }, 12000);

            this.waiter = {
                resolve: (message) => {
                    clearTimeout(timer);
                    resolve(message);
                },
                reject: (error) => {
                    clearTimeout(timer);
                    reject(error);
                },
            };
        });
    }

    async response(method: string, params: unknown, observe = this.observe): Promise<WireMessage> {
        const id = ++this.sequence;
        await this.send(method, params, id);

        for (;;) {
            const message = await this.receive();
            if (message.id === id) return message;
            await observe(message);
        }
    }

    async query<P, R, PR, E, RO>(
        type: ProtocolRequestType<P, R, PR, E, RO>,
        params: P,
        observe = this.observe,
    ): Promise<R> {
        const message = await this.response(type.method, params, observe);
        assert.equal(message.error, undefined, JSON.stringify(message));
        return message.result as R;
    }

    async initialize(
        root = 'file:///workspace',
        options: Record<string, unknown> = {},
        capabilities: ClientCapabilities = {},
    ): Promise<void> {
        const params: InitializeParams = {
            processId: null,
            rootUri: root,
            workspaceFolders: [{ uri: root, name: 'project' }],
            initializationOptions: options,
            capabilities,
        };
        await this.send('initialize', params, 1, true);
        const message = await this.receive();
        assert.equal(message.id, 1);
        assert.equal(message.error, undefined, JSON.stringify(message));

        const result = message.result as InitializeResult;
        assert.equal((result.capabilities.textDocumentSync as { change: number }).change, 2);
        assert.equal(result.capabilities.positionEncoding, 'utf-16');
        assert(
            result.capabilities.hoverProvider &&
                result.capabilities.definitionProvider &&
                result.capabilities.referencesProvider,
        );
        assert(
            result.capabilities.documentHighlightProvider &&
                result.capabilities.foldingRangeProvider,
        );
        assert(result.capabilities.callHierarchyProvider);
        assert.deepEqual(result.capabilities.completionProvider?.triggerCharacters, ['.', ':']);
        await this.send('initialized', {});
    }

    async diagnostics(uri: string, version: number) {
        const message = await this.receive();
        assert.equal(message.method, 'textDocument/publishDiagnostics');
        const params = message.params as PublishDiagnosticsParams;
        assert.equal(params.uri, uri);
        assert.equal(params.version, version);
        return params.diagnostics;
    }

    async finish(): Promise<void> {
        const response = await this.response('shutdown', {});
        assert.equal(response.error, undefined);
        assert.equal(response.result, null);

        await this.send('exit');
        this.child.stdin.end();
        assert.equal(await this.wait(), 0);
        assert.equal(this.stderr, '');
        assert.equal(this.messages.length, 0, 'duplicate response after shutdown');
    }

    async wait(): Promise<number | null> {
        let timer: NodeJS.Timeout | undefined;
        try {
            return await Promise.race([
                this.exited,
                new Promise<never>((_, reject) => {
                    timer = setTimeout(() => reject(new Error('LSP did not exit')), 5000);
                }),
            ]);
        } finally {
            clearTimeout(timer);
        }
    }

    async close(): Promise<void> {
        if (this.child.exitCode === null) {
            this.child.kill();
            await this.wait();
        }

        this.reader.dispose();
        this.writer.dispose();
    }
}

export function completions(value: CompletionItem[] | CompletionList | null): CompletionItem[] {
    assert(value && !Array.isArray(value), 'server supplies CompletionList');
    return value.items;
}

export function locations(value: unknown): Location[] {
    assert(Array.isArray(value) && value.every(Location.is));
    return value;
}

export function outlines(value: unknown): DocumentSymbol[] {
    assert(Array.isArray(value) && value.every(DocumentSymbol.is));
    return value;
}

export function actions(value: (CodeAction | Command)[] | null): CodeAction[] {
    assert(value && value.every((item) => 'kind' in item));
    return value as CodeAction[];
}

export function changes(
    edit: WorkspaceEdit | undefined,
): (Omit<TextDocumentEdit, 'edits'> & { edits: TextEdit[] })[] {
    assert(edit?.documentChanges && edit.documentChanges.every(TextDocumentEdit.is));
    return edit.documentChanges.map((document) => {
        assert(document.edits.every(TextEdit.is));
        return { ...document, edits: document.edits };
    });
}

export function edits(item: CompletionItem): TextEdit[] {
    assert(item.textEdit && 'range' in item.textEdit);
    return [item.textEdit, ...(item.additionalTextEdits ?? [])];
}

export function hoverText(hover: Hover | null): string {
    assert(hover && MarkupContent.is(hover.contents));
    return hover.contents.value;
}

export async function until(predicate: () => boolean, message: string): Promise<void> {
    const deadline = Date.now() + 5000;
    while (!predicate()) {
        assert(Date.now() < deadline, message);
        await delay(5);
    }
}

export async function hover(
    client: Client,
    uri: string,
    text: string,
    needle: string,
    id: number,
    expected: string,
): Promise<void> {
    const at = text.indexOf(needle);
    await client.send(
        'textDocument/hover',
        { textDocument: { uri }, position: position(text, at) },
        id,
    );
    const message = await client.receive();
    assert.equal(message.id, id);
    assert.equal(message.error, undefined);

    const result = message.result as Hover;
    assert.deepEqual(result.contents, { kind: 'plaintext', value: expected });
    assert.deepEqual(result.range, {
        start: position(text, at),
        end: position(text, at + needle.replace(/:$/, '').length),
    });
}
