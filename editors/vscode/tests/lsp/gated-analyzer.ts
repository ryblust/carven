import assert from 'node:assert/strict';
import { existsSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';

const root = process.env.CARVEN_ANALYZER_GATE;
assert(root);

const u32 = (value: number) => {
    const bytes = Buffer.alloc(4);
    bytes.writeUInt32BE(value);
    return bytes;
};

const u64 = (value: number) => {
    const bytes = Buffer.alloc(8);
    bytes.writeBigUInt64BE(BigInt(value));
    return bytes;
};

const text = (value: string) => {
    const bytes = Buffer.from(value);
    return Buffer.concat([u32(bytes.length), bytes]);
};

let pending = Buffer.alloc(0);
let version = 0;
let revision = 0;
let sourceText = '';

for await (const chunk of process.stdin) {
    pending = Buffer.concat([pending, chunk]);
    while (pending.length >= 4 && pending.length >= pending.readUInt32BE(0) + 4) {
        const size = pending.readUInt32BE(0);
        const body = pending.subarray(4, size + 4);
        pending = pending.subarray(size + 4);
        let cursor = 1;
        const method = body[0];

        const number = (width: number) => {
            const value =
                width === 8 ? Number(body.readBigUInt64BE(cursor)) : body.readUInt32BE(cursor);
            cursor += width;
            return value;
        };

        const string = () => {
            const size = number(4);
            const value = body.toString('utf8', cursor, cursor + size);
            cursor += size;
            return value;
        };

        const request: {
            uri?: string;
            version?: number;
            revision?: number;
            offset?: number;
            module?: string;
            text?: string;
        } = {};
        if (method === 4) {
            Object.assign(request, {
                uri: string(),
                module: string(),
                version: number(8),
                text: string(),
            });
        }
        if (method === 6) {
            Object.assign(request, { revision: number(8), uri: string(), offset: number(4) });
        } else if (method === 13) {
            Object.assign(request, { revision: number(8), uri: string() });
        }

        let payload = Buffer.alloc(0);
        if (method === 0) {
            payload = u32(8);
        } else if (method === 2) {
            payload = u32(0);
        } else if ([3, 4, 5].includes(method)) {
            if (method === 4) {
                if (revision === 0) {
                    writeFileSync(join(root, 'blocked'), '');
                    while (!existsSync(join(root, 'release'))) {
                        await delay(5);
                    }
                }

                version = request.version!;
                sourceText = request.text!;
                ++revision;
            } else if (method === 5) {
                ++revision;
            }

            payload = Buffer.alloc(20); // documents, extra diagnostic texts, diagnostics, warnings, watches
        } else if (method === 6) {
            payload = Buffer.concat([
                Buffer.from([1]),
                text(`v${version}`),
                text(request.uri!),
                u32(0),
                u32(1),
                text(''),
            ]);
        } else if (method === 13) {
            payload = Buffer.concat([
                u32(0), // symbols
                u32(1), // source documents
                text(request.uri!),
                text(sourceText),
                u64(version),
                Buffer.from([1]),
            ]);
        }

        const response = Buffer.concat([
            Buffer.from([1]),
            u64(revision),
            Buffer.from([0]),
            payload,
        ]);
        await new Promise<void>((resolve, reject) => {
            process.stdout.write(Buffer.concat([u32(response.length), response]), (error) =>
                error ? reject(error) : resolve(),
            );
        });
        if (method === 1) process.exit(0);
    }
}
