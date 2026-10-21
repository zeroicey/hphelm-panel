# .ai/ — project memory bank

Portable, tool-independent memory for AI coding tools. Pi reads and writes **this directory**, and
the `.ai/` contract is tool-agnostic: any agent that can read files can follow it, so switching
tools never resets context.

## Layer model (how context is loaded)

| Layer | What | Size rule | Loaded when |
| --- | --- | --- | --- |
| L0 dynamic injection | `engram dump` fingerprint | ≤ 1.5 KB / 300–500 tokens | every new session, pasted or injected |
| L1 behaviour contract | `AGENTS.md` | < 2 KB | always, by Pi |
| L2 knowledge bank | this `.ai/` tree | unbounded | on demand, via file reads |
| L3 action specs | `.ai/skills/*.md` | ~1 KB each | on trigger, via `/handoff`, `/audit`, … |

## Map

| Path | Responsibility | Never store |
| --- | --- | --- |
| `.ai/CURRENT_TASK.md` | what is being done right now: goal, checklist, state, blockers | long history |
| `.ai/ARCHITECTURE.md` | stack, directory boundaries, core call topology, invariants | step-by-step plans |
| `.ai/decisions/` | proposals and decisions with a status machine | casual chat |
| `.ai/sessions/` | condensed handoff snapshots for a fresh session to resume | full transcripts |
| `.ai/runbooks/` | deploy / env / CI / incident SOPs | one-off trivia |
| `.ai/pitfalls/cases/` | decoded failure modes, each with cause → fix → guard | duplicate bugs |
| `.ai/skills/` | canonical, portable skill specs (`/handoff`, …) | tool-specific config |

## Extending this bank

This directory is yours, not the tool's. Three zones decide what happens when you edit:

| Zone | Paths | `engram sync` |
| --- | --- | --- |
| tool-owned | the contract block in every rule file, generated carriers, `.engram/` | regenerated — do not hand-edit |
| shared, you own the input | this whole `.ai/` tree, `.ai/sections.json`, `.ai/skills/*.md` | read, never clobbered |
| yours alone | rule-file body above the contract, notes, anything outside the above | never touched |

**A new partition.** Declare it in `.ai/sections.json` — one edit puts it in the contract block
that `engram sync` regenerates, instead of you hand-editing a machine-owned section:

```json
{
  "version": 1,
  "sections": [
    {
      "name": "journal",
      "dir": ".ai/journal",
      "trigger": "what happened in last night's experiment window",
      "writeWhen": "a GPU window ends",
      "file": "YYYY-MM-DD-night-<slug>.md",
      "status": ["📝 DRAFT(auto)", "✅ REVIEWED"]
    }
  ]
}
```

`sync` renders the section into the contract's read and write tables and into `engram dump`;
`engram new journal "window 4"` scaffolds an entry from `.ai/journal/_TEMPLATE.md` when that file
exists. Delete the declaration and the rows disappear — the contract is derived, never authored, so
there is nothing to un-edit. Two things are refused, because they would change behaviour that
already exists: a `dir` inside a built-in partition (`decisions`, `sessions`, `runbooks`,
`pitfalls`, `skills`) or outside `.ai/`, and a `name` that is a built-in `engram new` verb
(`decision`, `session`, `pitfall`, `runbook`). Both warn and skip; nothing else is.

**A new skill.** Drop `.ai/skills/<name>.md` with `name` and `description` frontmatter, run
`engram sync`, and Pi's carriers follow. Delete the file and the carriers go with it. A
file whose frontmatter is missing, uses a YAML block scalar (`>` / `|`), or whose `name`
disagrees with the file name is reported and skipped — a skill that silently did not load is worse
than one that loudly did not. Two things are skipped in silence, on purpose: a file starting with
`_` (a template, like `_TEMPLATE.md`), and anything that is not a regular file.

## Operating contract (read before acting)

1. Read `.ai/CURRENT_TASK.md` first, then newest `.ai/decisions/`.
2. Pull `.ai/ARCHITECTURE.md` / `runbooks/` / `pitfalls/cases/` only when the trigger matches.
3. Write back in the same turn: decisions → `decisions/`, pitfalls → `pitfalls/cases/`, session
   ends → `sessions/`, state changes → `CURRENT_TASK.md`.
4. Status machine: `💭 PROPOSAL` → `✅ ACCEPTED` → `🪦 REJECTED`. Flip the line, keep the reasoning.
5. Never invent context you did not read. If the bank does not cover it, say so and ask.

## Naming

- `decisions/YYYY-MM-DD-<topic>.md`
- `sessions/YYYY-MM-DD-<topic>-handoff.md`
- `runbooks/<topic>.md`
- `pitfalls/cases/<case>.md`
- `_TEMPLATE.md` in each directory is the canonical shape — copy it, never edit in place.
- Skills: `.ai/skills/<name>.md`, `name` in `kebab-case` matching the command.

## Fresh-session bootstrap

```bash
engram dump            # ≤1.5 KB fingerprint → paste as the first user message
engram dump --json     # same content, machine-readable
```

Project: **hphelm-panel** · skeleton created 2026-10-21 by engram.
