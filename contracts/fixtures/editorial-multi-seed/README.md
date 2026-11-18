# Editorial multi-seed fixtures

Parametric Editorial variations: same Style Bible / news-reel templates, different
film seeds and headline copy. Motion jitter stays inside creative-variation safe
intervals; palette tokens come from the Editorial Style Bible only.

| Variant | Seed | Headline focus |
|---|---:|---|
| `variants/markets.authoring.json` | 42 | Markets open under pressure |
| `variants/rates.authoring.json` | 77 | Yields jumped overnight |
| `variants/flows.authoring.json` | 101 | Liquidity dried up fast |

`catalog.json` lists the locked variant set consumed by the multi-seed test.
Adding an unrelated shot to any variant must not change existing shot tracks.
