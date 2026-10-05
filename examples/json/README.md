# JSON and SIMD scanning

This example validates a complete JSON document, counts whitespace with a
constant byte set, and decodes escaped and plain quoted strings into independent
storage.

From the repository root:

```sh
./xmakew build
./xmakew run carven-example-json
```

Expected output:

```text
JSON is valid
Whitespace bytes: 3
Decoded: 你好 JSON
Decoded plain: plain JSON
```

See the [JSON API](../../crafts/carven/std/json/README.md) and
[SIMD scanning API](../../crafts/carven/std/simd/README.md#byte-set-scanning)
for input, error, and storage contracts.
