import assert from 'node:assert/strict';
import * as vscode from 'vscode';
import * as fs from 'node:fs';
import * as path from 'node:path';

const resultFile = process.env.CARVEN_LSP_TEST_RESULT;
assert(resultFile, 'The test launcher supplies CARVEN_LSP_TEST_RESULT');
const outputFile: string = resultFile;

function diagnosticsWhen(
    uri: vscode.Uri,
    predicate: (values: vscode.Diagnostic[]) => boolean,
): Promise<vscode.Diagnostic[]> {
    return new Promise((resolve, reject) => {
        const timeout = setTimeout(() => {
            listener.dispose();
            reject(
                new Error(
                    'Timed out waiting for diagnostics: ' +
                        uri.toString() +
                        '\n' +
                        JSON.stringify(vscode.languages.getDiagnostics(uri)),
                ),
            );
        }, 15000);

        const listener = vscode.languages.onDidChangeDiagnostics((event) => {
            if (!event.uris.some((value) => value.toString() === uri.toString())) return;
            const values = vscode.languages.getDiagnostics(uri);
            if (!predicate(values)) return;

            clearTimeout(timeout);
            listener.dispose();
            resolve(values);
        });
    });
}

async function replace(document: vscode.TextDocument, text: string) {
    const edit = new vscode.WorkspaceEdit();
    edit.replace(
        document.uri,
        new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)),
        text,
    );
    assert.equal(await vscode.workspace.applyEdit(edit), true);
}

async function continuousEditing(
    document: vscode.TextDocument,
    helper: vscode.Uri,
    consumer: vscode.Uri,
) {
    await replace(document, 'import .helper using provide;\nfn sample() { let value = prov');
    const editor = await vscode.window.showTextDocument(document);
    await vscode.commands.executeCommand('workbench.action.focusActiveEditorGroup');
    const end = document.positionAt(document.getText().length);
    editor.selection = new vscode.Selection(end, end);

    async function type(text: string) {
        assert(
            await editor.edit((edit) =>
                edit.insert(document.positionAt(document.getText().length), text),
            ),
        );
        const end = document.positionAt(document.getText().length);
        editor.selection = new vscode.Selection(end, end);
    }

    // Edit the real editor buffer while native suggestions and queries overlap.
    // TextEditor.edit is independent of the desktop window's keyboard focus.
    const queries: Thenable<unknown>[] = [];
    for (const text of ['i', 'd', 'e', '(', ')', ';', ' println(value);']) {
        await vscode.commands.executeCommand('editor.action.triggerSuggest');
        queries.push(
            vscode.commands.executeCommand(
                'vscode.executeHoverProvider',
                document.uri,
                document.positionAt(document.getText().indexOf('value')),
            ),
        );
        await type(text);
        await vscode.commands.executeCommand('hideSuggestWidget');
    }

    const broken = diagnosticsWhen(document.uri, (values) =>
        values.some((value) => value.message.includes('host_typing_error')),
    );
    await type(' host_typing_error(); }');
    assert(document.getText().includes('host_typing_error'), document.getText());
    await broken;

    const repaired = diagnosticsWhen(document.uri, (values) =>
        values.every((value) => value.severity !== vscode.DiagnosticSeverity.Error),
    );
    await replace(document, document.getText().replace(' host_typing_error();', ''));
    await repaired;
    await Promise.all(queries);

    const hover = await vscode.commands.executeCommand<vscode.Hover[]>(
        'vscode.executeHoverProvider',
        document.uri,
        document.positionAt(document.getText().lastIndexOf('value')),
    );
    assert(
        hover.some((item) =>
            item.contents.some(
                (content) => typeof content !== 'string' && content.value === '```carven\ni64\n```',
            ),
        ),
        JSON.stringify(hover),
    );

    const call = document.positionAt(document.getText().lastIndexOf('provide'));

    const names = await vscode.commands.executeCommand<vscode.CompletionList>(
        'vscode.executeCompletionItemProvider',
        document.uri,
        call.translate(0, 3),
    );
    assert(
        names.items.some((item) => item.label === 'provide'),
        JSON.stringify(names),
    );

    const definitions = await vscode.commands.executeCommand<vscode.Location[]>(
        'vscode.executeDefinitionProvider',
        document.uri,
        call,
    );
    assert.equal(definitions[0]?.uri.toString(), helper.toString());

    const references = await vscode.commands.executeCommand<vscode.Location[]>(
        'vscode.executeReferenceProvider',
        document.uri,
        call,
    );
    assert(
        references.some((item) => item.uri.toString() === consumer.toString()),
        JSON.stringify(references),
    );
    assert(
        !vscode.workspace.textDocuments.some((item) =>
            [helper.toString(), consumer.toString()].includes(item.uri.toString()),
        ),
    );
}

async function exercise() {
    const extension = vscode.extensions.getExtension('carven-lang.carven');
    assert.ok(extension);
    assert(vscode.workspace.workspaceFolders?.length);

    const workspace = path.dirname(vscode.workspace.workspaceFolders[0].uri.fsPath);
    const helper = vscode.Uri.file(path.join(workspace, 'src/helper.cv'));
    const consumer = vscode.Uri.file(path.join(workspace, 'src/consumer.cv'));
    fs.writeFileSync(
        consumer.fsPath,
        'import .helper using provide; fn consume() -> i64 => provide();',
    );
    const unopened = diagnosticsWhen(helper, (values) =>
        values.some((value) => value.severity === vscode.DiagnosticSeverity.Error),
    );
    const config = vscode.workspace.getConfiguration('carven');
    await config.update(
        'serverPath',
        path.join(path.dirname(outputFile), 'missing-toolchain', 'carven-lsp'),
        vscode.ConfigurationTarget.Workspace,
    );
    await extension.activate();
    await assert.rejects(
        async () => vscode.commands.executeCommand('carven.restartServer'),
        /ENOENT/,
    );

    // Correcting the executable restarts analysis in the same window.
    await config.update('serverPath', undefined, vscode.ConfigurationTarget.Workspace);
    await unopened;
    assert.ok(
        !vscode.workspace.textDocuments.some(
            (document) => document.uri.toString() === helper.toString(),
        ),
    );

    const main = await vscode.workspace.openTextDocument(
        vscode.Uri.file(path.join(workspace, 'src/main.cv')),
    );
    await vscode.window.showTextDocument(main);

    const fixed = diagnosticsWhen(helper, (values) =>
        values.every((value) => value.severity !== vscode.DiagnosticSeverity.Error),
    );
    fs.writeFileSync(
        helper.fsPath,
        'struct Point { width: i32, height: i64 }\nfn provide() -> i64 => 42;',
    );
    await fixed;

    async function typeOf(document: vscode.TextDocument, expected: string) {
        const hovers = await vscode.commands.executeCommand<vscode.Hover[]>(
            'vscode.executeHoverProvider',
            document.uri,
            document.positionAt(document.getText().lastIndexOf('value')),
        );
        assert.ok(
            hovers.some((hover) =>
                hover.contents.some(
                    (content) =>
                        typeof content !== 'string' &&
                        content.value === '```carven\n' + expected + '\n```',
                ),
            ),
            JSON.stringify(hovers),
        );
    }
    await typeOf(main, 'i64');

    const workspaceSymbols = await vscode.commands.executeCommand<vscode.SymbolInformation[]>(
        'vscode.executeWorkspaceSymbolProvider',
        'Point',
    );
    assert.ok(
        workspaceSymbols.some(
            (symbol) =>
                symbol.name === 'Point' && symbol.location.uri.toString() === helper.toString(),
        ),
        JSON.stringify(workspaceSymbols),
    );
    assert.ok(
        !vscode.workspace.textDocuments.some(
            (document) => document.uri.toString() === helper.toString(),
        ),
    );

    const outline = await vscode.commands.executeCommand<vscode.DocumentSymbol[]>(
        'vscode.executeDocumentSymbolProvider',
        main.uri,
    );
    assert.ok(
        outline.some((symbol) => symbol.name === 'sample'),
        JSON.stringify(outline),
    );

    // The included build script is outside both the opened src folder and the project.
    const buildScript = path.join(path.dirname(workspace), 'targets.lua');
    const originalConfig = fs.readFileSync(buildScript, 'utf8');

    async function waitForPoint(present: boolean) {
        const deadline = Date.now() + 15000;
        while (Date.now() < deadline) {
            const symbols = await vscode.commands.executeCommand<vscode.SymbolInformation[]>(
                'vscode.executeWorkspaceSymbolProvider',
                'Point',
            );
            if (
                symbols.some((symbol) => symbol.location.uri.toString() === helper.toString()) ===
                present
            )
                return;
            await new Promise((resolve) => setTimeout(resolve, 100));
        }
        throw new Error('Build-context symbol refresh timed out: ' + present);
    }

    fs.writeFileSync(buildScript, 'target("app"');
    await waitForPoint(false);

    await replace(main, main.getText() + '\r\n// edited while project configuration is broken');
    const unavailableDefinition = await vscode.commands.executeCommand<vscode.Location[]>(
        'vscode.executeDefinitionProvider',
        main.uri,
        main.positionAt(main.getText().lastIndexOf('provide')),
    );
    assert.equal(unavailableDefinition.length, 0);
    fs.writeFileSync(
        helper.fsPath,
        'struct Point { width: i32, height: i64 }\nfn provide() -> i32 => true;',
    );
    const recoveredErrors = diagnosticsWhen(helper, (values) =>
        values.some((value) => value.severity === vscode.DiagnosticSeverity.Error),
    );

    fs.writeFileSync(buildScript, originalConfig);
    await waitForPoint(true);
    await recoveredErrors;
    await typeOf(main, 'i32');
    assert(main.isDirty);
    assert(!fs.readFileSync(main.uri.fsPath, 'utf8').includes('edited while'));

    const recoveredFix = diagnosticsWhen(helper, (values) =>
        values.every((value) => value.severity !== vscode.DiagnosticSeverity.Error),
    );
    fs.writeFileSync(
        helper.fsPath,
        'struct Point { width: i32, height: i64 }\nfn provide() -> i64 => 42;',
    );
    await recoveredFix;
    await typeOf(main, 'i64');
    await vscode.commands.executeCommand('carven.reloadProject');
    await waitForPoint(true);

    const nextBuildScript = path.join(path.dirname(workspace), 'targets-next.lua');
    fs.writeFileSync(
        nextBuildScript,
        originalConfig.slice(originalConfig.indexOf('target("other")')),
    );
    const rootConfig = path.join(workspace, 'xmake.lua');
    fs.writeFileSync(
        rootConfig,
        fs
            .readFileSync(rootConfig, 'utf8')
            .replace(JSON.stringify(buildScript), JSON.stringify(nextBuildScript)),
    );
    await waitForPoint(false);
    const removed = await vscode.commands.executeCommand<vscode.Location[]>(
        'vscode.executeDefinitionProvider',
        main.uri,
        main.positionAt(main.getText().lastIndexOf('provide')),
    );
    assert.equal(removed.length, 0);
    fs.writeFileSync(nextBuildScript, originalConfig);
    await waitForPoint(true);
    await typeOf(main, 'i64');

    const validRoot = fs.readFileSync(rootConfig, 'utf8');

    fs.unlinkSync(rootConfig);
    await waitForPoint(false);

    fs.writeFileSync(rootConfig, validRoot);
    await waitForPoint(true);

    await continuousEditing(main, helper, consumer);

    await replace(
        main,
        'import .helper using provide;\r\nfn sample() { let value = provide(); println(value); }',
    );
    const before = main.getText();
    const offset = before.lastIndexOf('provide()');
    const edit = new vscode.WorkspaceEdit();
    edit.replace(
        main.uri,
        new vscode.Range(main.positionAt(offset), main.positionAt(offset + 'provide()'.length)),
        'provide() as i32',
    );
    edit.insert(main.uri, new vscode.Position(0, 0), '// 😀 incremental\r\n');
    assert.equal(await vscode.workspace.applyEdit(edit), true);
    await typeOf(main, 'i32');

    await replace(
        main,
        'import .helper using { Point, provide }\r\nfn sample(input: i32) { let _ = "😀"; let broken = ; let inferred = provide(); let point = Point { width: 1, height: 2 }; let width = point.width;',
    );
    const legend = await vscode.commands.executeCommand<vscode.SemanticTokensLegend>(
        'vscode.provideDocumentSemanticTokensLegend',
        main.uri,
    );
    const semantic = await vscode.commands.executeCommand<vscode.SemanticTokens>(
        'vscode.provideDocumentSemanticTokens',
        main.uri,
    );
    assert.ok(semantic.data.length > 0);
    let tokenLine = 0;
    let tokenCharacter = 0;
    const decoded = [];
    for (let index = 0; index < semantic.data.length; index += 5) {
        const [line, character, length, kind, modifiers] = semantic.data.slice(index, index + 5);
        tokenLine += line;
        tokenCharacter = line ? character : tokenCharacter + character;
        decoded.push({
            position: new vscode.Position(tokenLine, tokenCharacter),
            length,
            kind: legend.tokenTypes[kind],
            modifiers,
        });
    }

    const inferredPosition = main.positionAt(main.getText().indexOf('inferred'));
    assert.ok(
        decoded.some(
            (token) =>
                token.position.isEqual(inferredPosition) &&
                token.kind === 'variable' &&
                token.modifiers & 2,
        ),
        JSON.stringify(decoded),
    );

    const hints = await vscode.commands.executeCommand<vscode.InlayHint[]>(
        'vscode.executeInlayHintProvider',
        main.uri,
        new vscode.Range(main.positionAt(0), main.positionAt(main.getText().length)),
    );
    assert.ok(
        hints.some(
            (hint) =>
                hint.label === ': i64' &&
                hint.position.isEqual(main.positionAt(main.getText().indexOf('inferred') + 8)),
        ),
        JSON.stringify(hints),
    );
    assert.ok(
        !vscode.workspace.textDocuments.some(
            (document) => document.uri.toString() === helper.toString(),
        ),
    );

    await replace(
        main,
        'import .helper using provide as compute;\r\nfn sample() {\r\n    let broken = ;\r\n    compute();\r\n    compute(',
    );
    const documentHighlights = await vscode.commands.executeCommand<vscode.DocumentHighlight[]>(
        'vscode.executeDocumentHighlights',
        main.uri,
        main.positionAt(main.getText().lastIndexOf('compute')),
    );
    assert.equal(documentHighlights.length, 3);
    assert.ok(documentHighlights.every((item) => main.getText(item.range) === 'compute'));

    const folding = await vscode.commands.executeCommand<vscode.FoldingRange[]>(
        'vscode.executeFoldingRangeProvider',
        main.uri,
    );
    assert.ok(
        folding.some((item) => item.start === 1 && item.end >= 3),
        JSON.stringify(folding),
    );
    assert.ok(
        !vscode.workspace.textDocuments.some(
            (document) => document.uri.toString() === helper.toString(),
        ),
    );

    await replace(
        main,
        'import .helper using provide as compute; fn sample() { let broken = ; compute(',
    );
    const signature = await vscode.commands.executeCommand<vscode.SignatureHelp>(
        'vscode.executeSignatureHelpProvider',
        main.uri,
        main.positionAt(main.getText().length),
        '(',
    );
    assert.equal(signature.signatures[0].label, 'compute() -> i64');

    const hierarchy = await vscode.commands.executeCommand<vscode.CallHierarchyItem[]>(
        'vscode.prepareCallHierarchy',
        main.uri,
        main.positionAt(main.getText().lastIndexOf('compute')),
    );
    assert.equal(hierarchy.length, 1);
    assert.equal(hierarchy[0].name, 'provide');
    assert.equal(hierarchy[0].uri.toString(), helper.toString());

    const incoming = await vscode.commands.executeCommand<vscode.CallHierarchyIncomingCall[]>(
        'vscode.provideIncomingCalls',
        hierarchy[0],
    );
    assert.deepEqual(incoming.map((call) => call.from.name).sort(), ['consume', 'sample']);
    const caller = incoming.find((call) => call.from.uri.toString() === consumer.toString());
    assert(caller);

    const outgoing = await vscode.commands.executeCommand<vscode.CallHierarchyOutgoingCall[]>(
        'vscode.provideOutgoingCalls',
        caller.from,
    );
    assert.equal(outgoing.length, 1);
    assert.equal(outgoing[0].to.uri.toString(), helper.toString());
    assert.equal(outgoing[0].to.name, 'provide');
    assert.equal(outgoing[0].fromRanges.length, 1);
    assert.ok(
        !vscode.workspace.textDocuments.some((document) =>
            [consumer.toString(), helper.toString()].includes(document.uri.toString()),
        ),
    );

    const aliasEdits = await vscode.commands.executeCommand<vscode.WorkspaceEdit>(
        'vscode.executeDocumentRenameProvider',
        main.uri,
        main.positionAt(main.getText().lastIndexOf('compute')),
        'calculate',
    );
    assert.equal(aliasEdits.size, 1);
    assert.equal(await vscode.workspace.applyEdit(aliasEdits), true);
    assert.ok(main.getText().includes('provide as calculate'));
    assert.ok(main.getText().endsWith('calculate('));

    const exportedEdits = await vscode.commands.executeCommand<vscode.WorkspaceEdit>(
        'vscode.executeDocumentRenameProvider',
        main.uri,
        main.positionAt(main.getText().indexOf('provide')),
        'renamed',
    );
    assert.equal(exportedEdits.size, 3);
    assert.ok(exportedEdits.entries().some(([uri]) => uri.toString() === consumer.toString()));
    assert.ok(
        !vscode.workspace.textDocuments.some(
            (document) => document.uri.toString() === consumer.toString(),
        ),
    );

    await replace(
        main,
        '// 😀 format and quick fix\r\nimport .helper using provide; fn sample( )->i64{return provdie();}\r\n',
    );
    const formattedEdits = await vscode.commands.executeCommand<vscode.TextEdit[]>(
        'vscode.executeFormatDocumentProvider',
        main.uri,
        { tabSize: 4, insertSpaces: true },
    );
    assert.ok(formattedEdits.length > 0);
    const formatted = new vscode.WorkspaceEdit();
    formatted.set(main.uri, formattedEdits);
    assert.equal(await vscode.workspace.applyEdit(formatted), true);
    assert.ok(main.getText().includes('    return provdie();'));

    const quickFixes = await vscode.commands.executeCommand<vscode.CodeAction[]>(
        'vscode.executeCodeActionProvider',
        main.uri,
        new vscode.Range(main.positionAt(0), main.positionAt(main.getText().length)),
        vscode.CodeActionKind.QuickFix.value,
    );
    const spellingFix = quickFixes.find((action) => action.title === "Change to 'provide'");
    assert.ok(spellingFix?.edit, JSON.stringify(quickFixes));
    const quickFixed = diagnosticsWhen(main.uri, (values) =>
        values.every((value) => value.severity !== vscode.DiagnosticSeverity.Error),
    );
    assert.equal(await vscode.workspace.applyEdit(spellingFix.edit), true);
    await quickFixed;
    assert.ok(main.getText().includes('return provide();'));
    assert.ok(
        !vscode.workspace.textDocuments.some(
            (document) => document.uri.toString() === helper.toString(),
        ),
    );

    await replace(main, '// 😀 auto import\r\nfn sample() -> i64 => prov();');
    const autoNames = await vscode.commands.executeCommand<vscode.CompletionList>(
        'vscode.executeCompletionItemProvider',
        main.uri,
        main.positionAt(main.getText().lastIndexOf('prov') + 4),
    );
    const autoImport = autoNames.items.find(
        (item) => item.label === 'provide' && item.detail?.includes('helper'),
    );
    assert.ok(autoImport?.additionalTextEdits?.length, JSON.stringify(autoNames));
    assert.ok(autoImport.range instanceof vscode.Range);
    assert.equal(typeof autoImport.insertText, 'string');
    const autoImported = new vscode.WorkspaceEdit();
    autoImported.set(main.uri, [
        vscode.TextEdit.replace(autoImport.range, autoImport.insertText as string),
        ...autoImport.additionalTextEdits,
    ]);
    const importedDiagnostics = diagnosticsWhen(main.uri, (values) =>
        values.every((value) => value.severity !== vscode.DiagnosticSeverity.Error),
    );
    assert.equal(await vscode.workspace.applyEdit(autoImported), true);
    await importedDiagnostics;
    assert.ok(main.getText().includes('using provide;'));
    assert.ok(main.getText().endsWith('provide();'));

    await replace(
        main,
        'import .helper using Point;\r\nimport .helper using provide as compute;\r\nfn sample() -> i64 => compute();',
    );
    const organizeActions = await vscode.commands.executeCommand<vscode.CodeAction[]>(
        'vscode.executeCodeActionProvider',
        main.uri,
        new vscode.Range(main.positionAt(0), main.positionAt(main.getText().length)),
        vscode.CodeActionKind.SourceOrganizeImports.value,
    );
    const organizeImport = organizeActions.find(
        (action) => action.kind?.value === 'source.organizeImports',
    );
    assert.ok(organizeImport?.edit, JSON.stringify(organizeActions));
    assert.equal(await vscode.workspace.applyEdit(organizeImport.edit), true);
    assert.ok(!main.getText().includes('Point'));
    assert.ok(main.getText().includes('using provide as compute;'));
    assert.ok(
        !vscode.workspace.textDocuments.some(
            (document) => document.uri.toString() === helper.toString(),
        ),
    );
    await vscode.commands.executeCommand('workbench.action.revertAndCloseActiveEditor');

    const filename = path.join(path.dirname(outputFile), 'source.cv');
    const broken = 'fn main( {';
    fs.writeFileSync(filename, broken);
    for (const open of [
        () => vscode.workspace.openTextDocument(vscode.Uri.file(filename)),
        () => vscode.workspace.openTextDocument({ language: 'carven', content: broken }),
    ]) {
        // Register before opening: diagnostics may arrive during openTextDocument.
        const document = await open();
        const reported = diagnosticsWhen(document.uri, (values) =>
            values.some((value) => value.source === 'carven'),
        );
        await vscode.window.showTextDocument(document);
        const errors = await reported;
        assert.equal(errors[0].severity, vscode.DiagnosticSeverity.Error);
        const cleared = diagnosticsWhen(document.uri, (values) => values.length === 0);

        const source = '// 😀 Unicode position check\nfn sample(input: i32) -> i32 => input;';
        await replace(document, source);
        await cleared;
        const hovers = await vscode.commands.executeCommand<vscode.Hover[]>(
            'vscode.executeHoverProvider',
            document.uri,
            document.positionAt(source.lastIndexOf('input')),
        );
        assert.ok(
            hovers.some((hover) =>
                hover.contents.some(
                    (content) =>
                        typeof content !== 'string' && content.value === '```carven\ni32\n```',
                ),
            ),
            JSON.stringify(hovers),
        );
        assert.equal(document.getText(hovers[0].range), 'input');

        await vscode.commands.executeCommand('workbench.action.revertAndCloseActiveEditor');
    }
    assert.equal(fs.readFileSync(filename, 'utf8'), broken);
}

export async function run(): Promise<void> {
    try {
        if (process.env.CARVEN_VSIX) {
            const extension = vscode.extensions.getExtension('carven-lang.carven');
            assert(extension);
            assert(
                extension.extensionPath.startsWith(
                    path.join(path.dirname(outputFile), 'extensions') + path.sep,
                ),
            );
        }

        await exercise();
        await vscode.commands.executeCommand('carven.restartServer');

        const config = vscode.workspace.getConfiguration('carven');
        await config.update('target', 'other', vscode.ConfigurationTarget.Workspace);
        await vscode.commands.executeCommand('carven.reloadProject');
        const restricted = await vscode.commands.executeCommand<vscode.SymbolInformation[]>(
            'vscode.executeWorkspaceSymbolProvider',
            'provide',
        );
        assert(!restricted.some((item) => item.name === 'provide'));

        await config.update('target', undefined, vscode.ConfigurationTarget.Workspace);
        await vscode.commands.executeCommand('carven.reloadProject');
        const restored = await vscode.commands.executeCommand<vscode.SymbolInformation[]>(
            'vscode.executeWorkspaceSymbolProvider',
            'provide',
        );
        assert(restored.some((item) => item.name === 'provide'));

        fs.writeFileSync(outputFile, 'passed');
    } catch (error) {
        fs.writeFileSync(
            outputFile,
            error instanceof Error ? (error.stack ?? error.message) : String(error),
        );
        throw error;
    }
}
