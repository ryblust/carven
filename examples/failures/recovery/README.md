# Recover, translate, and keep useful failure data

A configuration loader should try a backup when a setting is absent, report
malformed input, and use a built-in default only when neither setting exists.
These are three different decisions.

## Follow a value through the layers

[parser.cv](parser.cv) parses decimal ASCII text into a port in 1..65535.
`MissingSetting` is separate from the `InvalidPort` enum. A bad digit includes
its zero-based byte position; range failures use a nullary case. The arithmetic
checks the upper bound before multiplying, so long numeric inputs cannot
silently overflow. Leading zeros are accepted; whitespace and signs are not.

[settings.cv](settings.cv) makes the recovery decisions:

- Private `select_port` tries the primary text. Only a missing value selects the
  backup. If backup parsing fails inside that handler, the failure goes outward
  rather than restarting the same `try`. Invalid primary text is rethrown.
- Shared `configured_port` translates both lower-level types into `ConfigError`.
  Its `Invalid` case stores the original `InvalidPort` payload. The callable's
  failure set is now the single declared type `ConfigError`.
- `port_or_default` uses value-form `try` to turn missing configuration into
  8080. Invalid configuration is rethrown with its payload intact.

[main.cv](main.cv) uses nested patterns to explain the failure. The outer call
still declares `ConfigError`, so it handles the complete enum, including
`Missing`; the contract does not narrow to a subset of enum cases merely
because this implementation recovers one case.

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-configuration
./xmakew run carven-example-configuration
```

## Output

```text
Primary setting
Port: 443
Backup setting
Port: 9000
Built-in default
Port: 8080
Invalid primary is not hidden
Bad digit at byte: 1
Invalid backup propagates
Port must be between 1 and 65535
Zero is not a port
Port must be between 1 and 65535
```

Inputs are supplied directly by `main`; the example does not read environment
variables or files. Its parser and recovery decisions are implemented in Carven,
without native C++ exception handling.

## Explore the policy

Replace the backup with `"9x"` when primary is empty. The final handler receives
byte position 1. Replace both inputs with empty text and the default is used.
A nonempty invalid primary intentionally does not fall back to a valid backup.

Change the default policy to `ConfigError(_) => 8080` to observe a different,
statically valid decision: invalid inputs now disappear into a default. The
compiler checks coverage and propagation; the application chooses which
failures it is appropriate to recover from.
