# Upstream integration, 2026-10-11

Target: `TheMoonPeople/ChronicleTwo:master`, fetched as `upstream/master` at
`4ee9003031d9379aebdb7dba6094ce000370d6f1`. Shared base is
`872e125032b0cdc4e95f7020ea028ff5dda38ca9`; the starting port has 401 commits
and upstream has five commits beyond that base.

## Plan

1. Commit the current host compatibility work and archive canonical state/handoff.
2. Create `port/master-sync-2026-10-11` from that checkpoint and merge upstream.
3. Review build/configuration, retail corrections and overlapping native implementations separately.
   Preserve working port behavior, checked generated-source transformations and regression coverage.
   Import upstream fixes without adding new retail logic; retail matching cannot run here.
4. Resolve conflicts explicitly, inspect submodule/API compatibility and adapt CI to actual targets.
5. Build the game and compatibility suite centrally; run the local PAL photo loader and source checks.
6. Commit and publish the integration branch, verify both parents are ancestors and the remote hash matches.

No gameplay launch is needed for this integration. The existing Items model-memory fault,
combat attack issue and recording-time capture limits remain separate open issues.

The user's fork `master` is a different divergent history (407 local / 332 fork commits at
preparation). Upstream integration alone does not promise that a pull request into that branch
will be conflict-free.

## Results

Integration and validation are pending.
