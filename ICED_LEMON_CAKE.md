# Iced Lemon Cake

A fork of [Lemonade](https://github.com/lemonade-sdk/lemonade) v2026.43.3 that adds three native backends.
Everything else is upstream Lemonade, unchanged. Part of NautiK9 Lemonade Stand
([lemonade-engines](https://github.com/festeraeb/lemonade-engines), where it is the "Iced lemon cake" engine).

| Backend | What it runs | Where the code is |
| --- | --- | --- |
| `burn` | lemonade-burn: BitNet b1.58 on a packed 2-bit ternary CPU kernel | `src/cpp/server/backends/burn/` |
| `cake` | Cake, through the `lemonade-cake-bridge` program; Cake itself is not included (FAIR licence) | `src/cpp/server/backends/cake/` |
| `freetoken` | FreeToken | `src/cpp/server/backends/freetoken/` |

The `burn` and `freetoken` backends were generated from one JSON spec each by
[`make-adapter`](https://github.com/festeraeb/lemonade-engine-adapters); `cake` is hand-written. A backend finds its
engine program on PATH when it loads a model; a missing program is an error, never a stand-in.

Build as upstream Lemonade. Licence: Apache-2.0, as upstream (`LICENSE`).
