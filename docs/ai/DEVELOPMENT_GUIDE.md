# DEVELOPMENT_GUIDE

## Workflow for a new format

1. Survey: extract samples to `out/` (git-ignored) or a scratch dir, inventory structure.
2. Write the reader in `tools/<format>.py` with a synthetic-data test in `tools/test_<format>.py`.
3. Verify locally against every matching file in `<BF1942_DIR>`; record the counts in the spec.
4. Write `docs/formats/<format>.md`; mark anything not verified.
5. Update SYSTEM_SUMMARY and add a PROJECT_MEMORY stage.

## Quality bar

- Tests and doc checks from the [SYSTEM_SUMMARY commands](SYSTEM_SUMMARY.md#commands) pass.

## Commits

- Imperative subject, body says why. One topic per commit; code and the docs it affects together.

## Doc-sync

| When you change | Update |
|---|---|
| A format reader | its `docs/formats/*.md`, SYSTEM_SUMMARY contracts |
| Files or folders in the tree | SYSTEM_SUMMARY tree |
| A roadmap step's state | SYSTEM_SUMMARY, both READMEs |
| A hard rule or stack choice | new ADR, ARCHITECTURE_NOTES, AGENTS.md |
| A risk or open question | `docs/roadmap/debt.md` |
| English README | Spanish `README.es.md` in the same commit |

## Memory entry template

```
## YYYY-MM-DD — topic
- what changed and why it mattered
- what was rejected
- what is left open
```
