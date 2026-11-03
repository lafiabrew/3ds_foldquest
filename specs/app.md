# First slice: FoldQuest

## Goal / magic moment
Original one-room clamshell puzzle whose state changes on actual suspend/resume, with touch/buttons and a complete solve/reset path.

## Demonstrable first slice
Actual APT lifecycle plus shared transition tests; no hidden work while lid closed. State stays in RAM through normal sleep; no saved-campaign/power-loss promise or misuse of diagnostic journaling as game-save storage. A user can start, interact, complete/reset or stop, and explicitly export truthful diagnostics. Source smoke alone is insufficient. Host scenario/core tests and actual native backend/artifacts required; physical observations separate.

## Exact observable acceptance
- `make test` drives actual feature core/UI/input and boundary/error/lifecycle assertions under ASan/UBSan.
- `python3 -m unittest discover -s tests -p test_contract.py -v` selects all named tests with no skips; dynamic/runner counts, not source-name guesses. Include deterministic behavior, invalid inputs, native acquisitions/cleanup and per-app journal recovery/adversarial tail/size/type tests.
- `make 3ds && make verify-3ds` builds genuine ARM ELF/SMDH/3DSX/CIA with ID `0x0D3F7` / `CTR-P-FOLD`, proper SMDH/CIA metadata and real application/SDK call graph. Test verifier with same-architecture stub/mutant, not only magic bytes.
- Diagnostic export actually creates `build/test_sd/3ds/logs/3ds_foldquest.log` using correct own app schema/build/seed/time/capabilities. Native journal code reused with per-app tests; active<=256KiB, first JSON record<=199 ASCII bytes, corruption never silently trimmed.
- Real Drop-host discovery/pull byte parity and exact source/log binding required by coordinator. No new duplicate server.
- Original assets/tunes and no copyrighted game packs; seeded/replay/simulation output reproducible where relevant.

## Boundaries
No save/config updates in this slice, no fake metrics/runtime backend, no host stub advertised as native functionality, no user-root paths/IDs in published text. Media/replay exports are new immutable files. Never claim physical functionality or finished asset-driven gameplay from compilation. Bounded work and visible failure, no infinite test loops. All required acceptance atomic expectations must clear>=95 plus deterministic/review gates.

## Exact field/recovery design
Run the full make/unittest chain above on an immutable pin. Deliberately invoke malformed/out-of-range input, unavailable native acquisition, shutdown/retry and unwritable/corrupt/oversize log state; assert visible failure and retained bytes then valid retry. Real SDK backend/header compile and actual production native control flow must exist. Hardware action/observation logs remain unverified until a real owner-console run.
