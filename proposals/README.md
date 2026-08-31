# Proposals

`proposals/` is Carven's design workspace and decision archive. A proposal
separates current repository facts, settled design, open questions, deferred
directions, implementation state, and validation. It does not define current
language or compiler behavior.

## Workspace layout

The workspace uses a flat layout. Each design domain is represented by as few
root-level Markdown files as its independent decisions require. Workspace
guidance lives in `README.md`, `TEMPLATE.md`, and `roadmap.md`.

```text
proposals/
    README.md
    TEMPLATE.md
    roadmap.md
    <design-domain>.md
```

`README.md` defines this workspace. `TEMPLATE.md` defines the shared proposal
shape. `roadmap.md` records ordering and dependencies across proposal domains.
Each other Markdown file records one proposal domain or one retained decision.

Use the shortest unambiguous filename for the domain. Keep syntax, semantics,
compiler facts, lowering, and backend choices together when they implement one
design authority. Split a domain only when each part can be decided and
implemented independently; record the dependency in the roadmap. Reusable
research may also be summarized in `notes/`, while the proposal remains
self-contained.

## Proposal structure

Active proposals start from [the template](TEMPLATE.md), which gives the
recommended reading order and metadata. Omit a section when it has no
information for the reader. A completed proposal may become a shorter decision
record after implementation and documentation handoff.

Each section records only information available at the proposal's current
maturity. Do not invent alternatives or delivery work to fill a template.

`Decision record` contains consequential settled choices and rationale under
stable identifiers. `Open decisions` orders unresolved questions by dependency
and identifies the next unblocked question. `Deferred work` preserves valid but
inactive directions under stable `DEFER-*` identifiers. `Design` remains the
readable specification; the decision record may be a compact index into it.

One open or deferred identifier covers one dependency set and activation
condition. Top-level metadata describes the whole proposal; slice-specific
status and dependencies stay with the slice. `Design` records the selected
behavior, while `Implementation` records delivery.

Change the shared structure when repeated use shows that a kind of information
has no clear home or that a section impairs readability.

## Lifecycle

Proposal status describes design maturity independently of implementation
progress:

- **Exploration** — the problem, boundary, and viable choices are being
  established. `Design` may explicitly point to the first open decision.
- **Draft** — a coherent design exists, or the domain has mixed-maturity
  slices, while one or more required active choices remain open.
- **Accepted** — all required active design decisions are closed and
  implementation may proceed. Valid inactive directions may remain in
  `Deferred work`.
- **Deferred** — work is paused with a recorded reason and reactivation
  condition. The document preserves the current design and open frontier.
- **Superseded** — another proposal replaces the design and is named in the
  status metadata.

Implementation progress is recorded separately as `Not started`, `In
progress`, `Partial`, `Complete`, or `Not applicable`.

An active proposal remains here while it has unresolved design work or accepted
scope awaiting handoff. Once it is fully implemented and documented, it leaves
the active proposal set. Git history normally preserves the rationale; retain
an archived proposal when that rationale has continuing project value.
