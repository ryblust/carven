# E2E Test Model Refactoring

## Goal

Align e2e test code paths with the production install layout, eliminating
divergent rule-file resolution and scattered temporary files between tests and
runtime.

## Current State

### Three carven.lua Resolution Paths

The rule file `xmake/rules/carven.lua` is located through three independent
mechanisms:

| Entry point | Mechanism |
|-------------|-----------|
| Batch e2e tests | `copy_file("xmake/rules/carven.lua", ...)` — hardcoded relative path from cwd |
| Single-file driver path | `find_ancestor({}, ...)` — upward search from cwd |
| Production install | `<prefix>/share/carven/xmake/rules/carven.lua` — installed layout |

No path validates any other. The upward search (`find_ancestor`) and
`CARVEN_RULE` env var exist solely to support development mode.

### Scattered Temporary Files

| Path | Cleaned |
|------|---------|
| `{TMP}/carven_e2e_batch/` | Yes, before setup |
| `{TMP}/carven_e2e_single_file.cv` | No |
| `{TMP}/.xmake/{hash}/carven-e2e-home/carven/scripts/{hash}/` | No |

Additionally, `os.tmpdir()` on Windows returns `Temp/.xmake/{hash}` rather than
bare Temp, making the effective cache directory unpredictable.

### `find_ancestor` Dependency

Three callers depend on `find_ancestor`:

| Caller | Purpose |
|--------|---------|
| `find_installed_rule` | Dev-mode fallback from binary dir |
| `find_carven_rule` | Cwd-based fallback |
| `find_project_root` | Project root discovery |

All three implicitly accept a directory and search upward. This behavior is
opaque — it may silently bind to an unintended parent directory.

## Candidate Approaches

### Rule File Resolution

**Compile-time injection for tests only.** Tests receive
`CARVEN_E2E_RULE_PATH` as a define. Production keeps
`<prefix>/share/carven/xmake/rules/carven.lua`. Dev-mode fallbacks
(`find_ancestor`, `CARVEN_RULE`) are removed.

**Pure install path.** Both tests and production use the installed
layout only. Tests must `xmake install` before running. No injection.

**Binary-relative derivation.** Derive the rule path from the binary
location in both dev and production
(`<binary>/../../share/carven/...` or a similar formula). No env vars or
fallbacks.

### Test Directory Layout

**Under `build/.e2e-test/`.** Under the project build directory, git-ignored,
predictable across platforms.

**Under `os.tmpdir()` but explicitly formed.** Use `os.tmpdir()` but
construct the path manually (`{TMP}/carven-e2e/`) rather than relying on the
xmake runner's `set("runenv", ...)` indirection.

**Under platform cache convention.** Use `user_cache_dir()` as the test
root, matching what production already uses.

### `find_project_root` Behavior

**Direct check only.** Check only whether `start / "xmake.lua"`
exists. Matching xmake's own behavior.

**Keep upward search.** Keep `find_ancestor` for project root
discovery but remove it from rule-file resolution.

### `find_ancestor` Scope

**Complete removal.** Remove `find_ancestor` entirely. Replace each
call site with an explicit check.

**Keep as internal utility.** Remove from the module interface but
keep it available for future use cases that genuinely need upward search.

## Open Questions

- [ ] Rule file resolution approach
- [ ] Test directory layout approach
- [ ] Whether `find_project_root` should check only the current directory
      or keep upward search
- [ ] Whether `find_ancestor` should be removed entirely or kept as an
      internal utility
- [ ] Cleanup strategy: per-test cleanup, global teardown, or leave artifacts
      for inspection?
