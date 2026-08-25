<!--
Thanks for contributing to SuGar Engine.

Please read CONTRIBUTING.md before filling this in. The checklist below is the
Decision Checklist from DevDocs/RULES.md — it is what the change is judged against,
not a formality.
-->

## What this changes

<!-- One paragraph. What is different after this merges? -->

## Why

<!--
SuGar is developed evidence-first. Pick the one that applies and fill it in:

- Defect: what the reproduction is, and (if there is one) the defect number.
- Game-forced capability: which game, what it could not do, what the workaround was.
- Measured performance fix: before/after numbers and how you measured.
- Docs: what was wrong or missing.

A change with none of these is a hard sell — see CONTRIBUTING.md, "What SuGar accepts".
-->

## The seam (Rule 22)

<!--
If this hardcodes a mode, format, policy, pipeline or branch: what category is it one
case of, and where would the second case force a change? If you deliberately took the
local fix instead, say so and why.

If you consulted a reference engine for where the seam belongs, cite which engine and
which of its DOCS. Rule 23 — never source you are not licensed to read for this purpose,
and never Nu Game Engine source at all.
-->

## Verification

Gate result (`SUGAR_VALIDATE=1`, headless, no GPU) — paste the final line for both:

```
Debug:   === N/N checks passed, 0 failure(s) ===
Release: === N/N checks passed, 0 failure(s) ===
```

**Rule 9a — what did you break to prove the new test can fail?**

<!--
e.g. "Neutered PhysicsWorld::step → Physics test FAIL". A green test proves nothing
until you have seen it go red for the intended reason. If this PR adds no test, say why.
-->

Also ran (delete what does not apply): `SUGAR_STRESS` · `SUGAR_SELFTEST` ·
`SUGAR_UITEST` · `SUGAR_BENCH` (Release) · `SUGAR_STRICT` · `SUGAR_COOK` ·
`SUGAR_PACKAGE` · played a real game with `SUGAR_GAME`

## Decision Checklist

- [ ] Does it make developers faster? (Rule 1)
- [ ] Does it improve the architecture? (Rule 2)
- [ ] Does it reduce complexity? (Rule 8)
- [ ] Is it testable — and has its test been shown to fail? (Rule 9a)
- [ ] Is it deterministic? (Rule 10)
- [ ] Does it fit the existing philosophy?
- [ ] Does it introduce unnecessary dependencies? (Rules 3, 4)
- [ ] Is documentation updated — `README.md`, `ROADMAP.md`, and
      `REQUIREMENTS_AND_SCOPE.md` if a boundary moved? (Rule 19)
- [ ] Can future contributors understand it? (Rule 20)

## Invariants touched

- [ ] No new authoritative state outside ECS, or outside what serialization can rebuild (Rule 21)
- [ ] `SuGarCore` still compiles with no Vulkan / ImGui / renderer / editor (Rule 15)
- [ ] No on-disk format change — or the `Serializer` golden test was updated **on purpose**
      and this PR says so (Rule 9b)
- [ ] No game-module ABI change — or `Game.dll` rebuild requirements are called out
- [ ] No project-specific gameplay added to the engine (Rule 16)

## Anything reviewers should push back on

<!-- Where are you least sure? Naming it gets you a better review. -->
