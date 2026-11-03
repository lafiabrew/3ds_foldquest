# FoldQuest — parallel first-release plan

## T1 — Usable first slice, not bootstrap acceptance
- Status: pending.
- Objective: Original one-room clamshell puzzle whose state changes on actual suspend/resume, with touch/buttons and a complete solve/reset path.
- Areas: src/include/tests/Makefile/meta/scripts/README/specs, declared actual engine/dependencies where applicable; vendor shared journal only as reviewed dependency.
- Validation: make test && python3 -m unittest discover -s tests -p test_contract.py -v && make 3ds && make verify-3ds && git diff --check. 
- Acceptance: Actual APT lifecycle plus shared transition tests; no hidden work while lid closed. State stays in RAM through normal sleep; no saved-campaign/power-loss promise or misuse of diagnostic journaling as game-save storage. Native module must be called by main, not only linked. Correct UID/artifacts, new error/retry/cleanup tests and actual own-app emitted log. Existing journal smoke preserved. Native data/device tests clearly labeled. Candidate stays in_progress after local checks until external exact-pin review.
- Demo: start real session, exercise actual controls/objective or recording/query/player, end/reset, explicitly export actual diagnostics; no synthetic runtime success markers.

## T2 — Review/API/demo/publication handoff
- Status: pending.
- Areas: exact code pin/real logs/native artifacts, review/report/doc evidence; no author moves to parent/root.
- Validation: repeat explicit T1 chain on final SHA; coordinator real Drop daemon api/logs/api/pull byte parity; current scoped RUGGED, fresh spec and quality PASS, mean atomic acceptance>=95; final docs audit. Confirm remote source SHA when coordinator publishes.
- Acceptance: source/license/privacy/genuine engine and actual behavior proved; source/evidence refs persistent. Public WIP is not accepted release. Physical tier remains separate, no phantom time/serial speedup.

## Boundaries and shared state
Read specs/app.md and AGENTS.md. User consent extends to this repo; no data upload by default. Source/failure evidence stays in durable workspace/ref, no temp-only work. Root constitution approved diagnostic journal exception only; old saves remain protected. The coordinator supplies read-only references/current image and owns timing, independent reviewers and publication. Work exactly one task/iteration; loop commits exact owned paths. Author model `claude-sonnet-5.5`; source identity `0x0D3F7` / `CTR-P-FOLD`.
