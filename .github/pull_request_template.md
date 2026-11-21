## Summary

<!-- What changes and why, in declarative sentences. Closes #N. -->

## Triage

<!-- AGENTS.md §3: which party owns the problem (mcpp, mcppls, backend or one plugin)?
     A workaround for another party's problem states its cause, the upstream issue,
     why it cannot wait and when it is removed. Write "n/a" for a pure refactor. -->

## Criteria

<!-- For each change: the test that fails before and passes after, and where it ran
     (local system or CI job). A criterion left to CI says so. -->

## Compatibility

<!-- What a user or a client observes after the upgrade: a protocol change, a changed
     setting or default, a migration, the oldest mcpp or editor still supported. -->

## Checklist

- [ ] One thing; the title is `<type>(<component>): <sentence>` (or `X.Y.Z: <sentence>`).
- [ ] The protocol schema, docs (README in both languages), `spec/i18n` and CHANGELOG.md follow the behaviour.
- [ ] `spec/` is edited, not its output; `mcpp-ide-dev gen vscode` leaves no diff.
- [ ] No commit carries an attribution trailer (`git log origin/main..HEAD -i --grep=Co-Authored-By` is empty).
