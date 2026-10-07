# JSON document editing

This example reads an owned JSON object, accesses its decoded message, updates
members, and writes the resulting document. JSON scanning uses the selected SIMD
backend.

From the repository root:

```sh
./xmakew build
./xmakew run carven-example-json
```

Expected output:

```text
你好 JSON
{"message":"你好 JSON","ok":true,"attempts":3}
```

See the [JSON API](../../crafts/carven/std/json/README.md) for value, error, and
storage contracts.
