# TIMAERT START HERE - MAIN DOCUMENTS - 2026-05-15

This is the root-level index for the current Timaert/Samosbor work cycle.
Open this file first. The important documents are intentionally near the repo
root so they are not hidden inside `Docs/Reports`.

Workspace:

```text
C:\Timaert\timaert_c
```

## Main Root Document

Read this first for the full commit/review plan:

```text
TIMAERT_MASTER_CHANGESET_AND_COMMIT_MANIFEST_2026-05-15.md
```

What it contains:

- Latest dirty-tree snapshot.
- Explicit warning about what is verified, partial, or environment-sensitive.
- Late changes that landed after the first reports.
- Commit split plan.
- Hunk-staging warnings for shared files.
- Final test/smoke gate before commits.

## Detailed Supporting Reports

These remain under `Docs/Reports` for archival order, but the root manifest
links to them and this index names them explicitly:

```text
Docs/Reports/2026-05-15_PRECOMMIT_CYCLE_DOCUMENTATION.md
Docs/Reports/2026-05-15_CHANGESET_INVENTORY.md
Docs/Reports/2026-05-15_AGENT_VERIFICATION_MATRIX.md
```

Use them as follows:

- `PRECOMMIT_CYCLE_DOCUMENTATION`: narrative summary of what changed and why.
- `CHANGESET_INVENTORY`: dirty-tree/file inventory and domain grouping.
- `AGENT_VERIFICATION_MATRIX`: agent-by-agent evidence, tests, and risk labels.

## Primary Evidence Folders

```text
Docs/Tasks/Status_TMA_*.md
Docs/AgentLogs/LOG_TMA_*.md
Docs/AgentLogs/Rationale_TMA_*.md
Docs/AgentLogs/integrator_*.log
```

## Hecton Boundary

Timaert/Samosbor docs belong in Timaert:

```text
C:\Timaert\timaert_c
```

Do not write Timaert docs to:

```text
C:\hades\Hecton8
```

Imported Hecton-origin reference material must remain narrow and clearly
labelled under Timaert quarantine folders. The broad `Docs/Imported/` mirror was
removed before push because it was unrelated Hecton carryover, not a Timaert
working document set.
The `Imported_Hecton8` quarantine buckets were also excluded from the first
push for the same reason.

## Current Honest Status

- The codebase has broad verified native C++ coverage across feature layer,
  roads/rivers/zones, spells, quests/save, paperdoll, audio, combat, subworld,
  and UI surfaces.
- The project is not globally "100% TS parity" until the remaining export walk
  in `translation.md` is complete.
- The current tree is ready for careful commit slicing, not a single bulk
  commit.
