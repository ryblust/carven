const vscode = require('vscode');
const { LanguageClient, State } = require('vscode-languageclient/node');

let client;
let transition = Promise.resolve();
let closing = false;

async function disposeClient() {
    const previous = client;
    client = undefined;
    if (!previous) return;

    // LanguageClient cannot dispose a failed start with no server connection.
    if (previous.state === State.StartFailed && previous.serverProcess === undefined) return;
    await previous.dispose();
}

async function activate(context) {
    const output = vscode.window.createOutputChannel('Carven', { log: true });
    const status = vscode.languages.createLanguageStatusItem('carven.project', {
        language: 'carven',
    });
    status.name = 'Carven project';
    status.command = { title: 'Reload Carven Project', command: 'carven.reloadProject' };
    context.subscriptions.push(output, status);

    function show(state, detail) {
        status.text =
            state === 'loading'
                ? 'Carven: Loading'
                : state === 'ready'
                  ? 'Carven'
                  : 'Carven: Unavailable';
        status.detail = detail;
        status.busy = state === 'loading';
        status.severity =
            state === 'error'
                ? vscode.LanguageStatusSeverity.Error
                : vscode.LanguageStatusSeverity.Information;
    }

    async function replaceClient() {
        await disposeClient();
        if (closing) return;

        const configuration = vscode.workspace.getConfiguration('carven');
        const next = new LanguageClient(
            'carven',
            'Carven',
            {
                command: configuration.get('serverPath', 'carven-lsp'),
            },
            {
                outputChannel: output,
                initializationOptions: {
                    xmakePath: configuration.get('xmakePath', 'xmake'),
                    target: configuration.get('target', ''),
                    projectStatus: true,
                },
                documentSelector: [
                    { scheme: 'file', language: 'carven' },
                    { scheme: 'untitled', language: 'carven' },
                ],
            },
        );
        client = next;

        next.onNotification('carven/projectStatus', (value) => {
            if (client === next) show(value.state, value.detail);
        });
        next.onDidChangeState((event) => {
            if (client !== next) return;
            if (event.newState === State.Starting) {
                show('loading', 'Starting language server');
            } else if (event.newState === State.Stopped) {
                show('error', 'Language server stopped. Use Carven: Restart Language Server.');
            }
        });

        show('loading', 'Starting language server');
        try {
            await next.start();
        } catch (error) {
            show('error', String(error));
            throw error;
        }
    }

    function restart() {
        const operation = transition.then(replaceClient);
        transition = operation.catch((error) => {
            console.error('Carven language server:', error);
        });
        return operation;
    }

    context.subscriptions.push(
        vscode.commands.registerCommand('carven.restartServer', restart),
        vscode.commands.registerCommand('carven.reloadProject', async () => {
            await transition;
            if (client) {
                await client.sendRequest('workspace/executeCommand', { command: 'carven.reload' });
            }
        }),
        vscode.workspace.onDidChangeConfiguration((event) => {
            if (
                ['serverPath', 'xmakePath', 'target'].some((key) =>
                    event.affectsConfiguration('carven.' + key),
                )
            ) {
                void restart().catch(() => {});
            }
        }),
    );

    // A startup failure leaves the extension available for a later restart.
    await restart().catch(() => {});
}

async function deactivate() {
    closing = true;
    await transition;

    await disposeClient();
}

module.exports = { activate, deactivate };
