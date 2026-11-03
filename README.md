# FoldQuest for Nintendo 3DS

**WIP — implementation lane assigned; not a playable/released/device-tested app yet.**

Original one-room clamshell puzzle whose state changes on actual suspend/resume, with touch/buttons and a complete solve/reset path.

Part of [lafiabrew](https://github.com/lafiabrew). UniqueId `0x0D3F7`, ProductCode `CTR-P-FOLD`. Each app is a separate repository. Native logging reuses the reviewed journal with per-app integration tests; media/replays are new immutable files, not replacements of user saves. No private capture/upload by default.

## Build status and source license
`make journal-check` is a real shared-runtime signal only. Current `make test`, `make 3ds` and `make verify-3ds` deliberately fail until the lane implements actual behavior and artifacts. Passing the smoke is not a completed app.

New original code/runtime scaffold is MIT. Built-in artwork/music/levels must be original or explicitly licensed.

## Required first-release gate
```sh
make test && python3 -m unittest discover -s tests -p test_contract.py -v && make 3ds && make verify-3ds && git diff --check
```



See [scope](specs/app.md), [operational instructions](AGENTS.md), [shared safety](specs/CONSTITUTION.md), and [plan](IMPLEMENTATION_PLAN.md). All current status is WIP until verified; no 3DS hardware claims from host models.
