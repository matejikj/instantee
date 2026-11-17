# Designated integer encoding (v1)

Shared rule for values that must survive TypeScript ↔ C++ round-trips without
silent precision loss. Applies to authoring and Scene IR `seed` today; the same
encoding is reserved for any future designated `uint64` field.

## Forms

| Form | Allowed range | Notes |
|---|---|---|
| JSON number | `0` … `9007199254740991` (`2^53 − 1`) | Exact in IEEE-754 binary64 / JS `Number` |
| JSON string | decimal `0` … `18446744073709551615` | No leading zeros except `"0"`; overflow rejected |

Prefer the number form when the value fits the safe range. Prefer the decimal
string once the value exceeds `2^53 − 1`, including the full `uint64` maximum.

## Non-goals

- Content hashes stay lowercase hex SHA-256 strings, not this encoding.
- Frame indices in documents remain JSON integers within schema bounds; C++
  `FrameIndex` still uses checked `uint64` arithmetic at runtime.
- Full document JSON parsing in C++ is a later deliverable; this policy and the
  C++ encode/decode helpers are the production contract for designated integers
  until then.
