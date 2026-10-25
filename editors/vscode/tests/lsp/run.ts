import { test } from 'node:test';

for (const name of [
    'protocol',
    'project',
    'documents',
    'navigation',
    'recovery',
    'refactor',
    'interop',
    'cpp-imports',
    'calls',
    'call-values',
    'imports',
    'edits',
    'decorations',
    'structure',
    'context',
]) {
    await test(name, { timeout: 60000 }, async () => {
        await import(`./${name}.ts`);
    });
}
