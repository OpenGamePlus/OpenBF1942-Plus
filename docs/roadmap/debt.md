# Debt, risks, open questions

| Item | Kind | Notes |
|---|---|---|
| RFA `unk` directory field | open question | constant per archive; meaning unknown |
| Patch archive override order | unverified | assumed `_NNN` archives override base files |
| `rfa.py extract` to disk on large archives | untested | full-install check was in-memory only |
| Pure-Python LZO speed | accepted debt | ~2 min for non-sound archives; fine for tools |
| CON level load sequence and object file order | unverified | assumed, see [con.md](../formats/con.md#what-the-engine-loads-by-itself) |
| Loose files vs archives priority | unverified | `con.py` assumes loose files win |
| CON method case-insensitivity | unverified | subsystems verified, methods assumed |
| SM collision blocks, footer, v10 byte, `alpha` field | open question | parsed as opaque, see [sm.md](../formats/sm.md) |
| No model viewer in the engine | deliberate | step 3 exports OBJ for Blender; rendering starts with the map viewer |
| Networking protocol | risk | undocumented; roadmap step 8 |
