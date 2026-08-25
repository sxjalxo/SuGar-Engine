# Governance

## Current state: one maintainer

SuGar Engine is maintained by **Sujal Garje** ([@sxjalxo](https://github.com/sxjalxo)).
There is no committee, no voting, and no formal membership tier. Saying so plainly is
more useful than describing a structure that does not exist.

That means:

* The maintainer has final say on what merges.
* Review latency depends on one person's availability. Please be patient, and ping a PR
  that has gone quiet for two weeks.
* The project is not currently looking for co-maintainers, but that will change as
  contribution volume grows. If you land several substantial changes, expect an
  invitation.

The project began as a final-year project and is developed in the open as a long-running
engine. It is not backed by a company, and there is no funding to allocate.

## How decisions actually get made

SuGar is unusual in that most of the decision-making is already written down and is not
re-litigated per pull request.

**[`DevDocs/RULES.md`](DevDocs/RULES.md) is the constitution.** Its 23 rules are
constraints, not goals. A proposal that conflicts with a rule has two honest paths:
change the proposal, or argue that the *rule* is wrong. Both are legitimate; the second
is a much bigger conversation and belongs in an issue, not a PR description.

**[`DevDocs/REQUIREMENTS_AND_SCOPE.md`](DevDocs/REQUIREMENTS_AND_SCOPE.md) bounds the
dependencies.** Every library has a written responsibility and an explicit list of what
it may not do. Expanding a library's scope is a governance decision, not an
implementation detail.

**Evidence outranks opinion.** Since M4 the engine has been developed by dogfooding: real
games are built on it, and engine work is what those games force. In practice this
settles most arguments, because the question stops being *should an engine have this* and
becomes *what could this game not do, and what did it cost*. Rule 18 says the same thing
about performance: measure first.

The consequence worth stating up front — **a good idea with no forcing case usually loses
to no change at all.** That is deliberate (Rule 8: deleting complexity is progress), and
it is not a comment on the idea's quality.

**Design records precede large changes.** Substantial seams get a written record in
`DevDocs/DESIGN_*.md` *before* the code — what the problem is, what was measured, what
was considered, what was chosen and why. Several of those records have been proven wrong
by writing the failing test afterwards, which is exactly what they are for.

## Changing the rules

`RULES.md` has grown by discovery, not by design: rules were added when the same class of
mistake showed up two or three times in different subsystems. Rule 21b, for instance,
exists because the same history-versus-present confusion arrived three times wearing three
different symptoms.

So to propose a rule change:

1. Open an issue titled `rules: …`.
2. Show the cases. A rule change wants two or more concrete incidents, not a principle.
3. Say what the rule would forbid or permit that it does not today, and what existing code
   would violate it.

Rules are not changed inside a feature PR.

## Roles

| Role | Who | What they can do |
| --- | --- | --- |
| Maintainer | Sujal Garje | Merge, release, tag, set direction, enforce the Code of Conduct |
| Contributor | anyone with a merged PR | Everything in [`CONTRIBUTING.md`](CONTRIBUTING.md) |

Contributors are credited in release notes and in the git history under their own name and
identity. Attribution is never rewritten or squashed away.

## Releases

Pre-1.0. `main` is the only supported branch; there are no maintained release branches or
backports. The version string lives in `CMakeLists.txt` (`SUGAR_VERSION`) and is baked
into crash reports along with the git commit, so any build can be traced to a commit.

Milestones (M1–M4) are tracked in [`ROADMAP.md`](ROADMAP.md). M4 is open-ended by design —
it runs until the engine is serious, and its tiers close when their questions stop
producing answers, not on a schedule.

## Code of Conduct

Enforcement is the maintainer's responsibility. See
[`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md), including what to do if a report concerns the
maintainer.
