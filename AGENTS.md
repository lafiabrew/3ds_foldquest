# Agent operations — FoldQuest

Own only this repository/jj lane. Read IMPLEMENTATION_PLAN.md and specs/app.md. Do not modify sibling/parent repositories or any existing root AGENTS. Search actual upstream/dependencies before duplicating capability.

- Deliver actual interactive native behavior: Actual APT lifecycle plus shared transition tests; no hidden work while lid closed. State stays in RAM through normal sleep; no saved-campaign/power-loss promise or misuse of diagnostic journaling as game-save storage.
- Exact gate: make test && python3 -m unittest discover -s tests -p test_contract.py -v && make 3ds && make verify-3ds && git diff --check.
- Engine/audio lanes must prove genuine declared source/build/asset licensing via tests/verify_engine.py. No wrappers/lookalikes advertised as ports. No proprietary/unknown assets or user ROM data downloaded/bundled.
- Preserve vendored runtime origin/hashes/notices. New per-app log formatter/adapter tests must validate actual journal integration, not blindly copy LinkLab metadata/seed rules.
- Diagnostic JSONL uses schema/app/build/event/elapsed_us/seed/settings/capabilities, first record<=199 ASCII bytes, active<=256KiB, fixed own app .log in 3ds/logs for existing Drop API. No user identities/addresses/home paths; relative owned artifact names may identify opt-in capture outputs.
- No default raw camera/mic recording/uploads. Immutable media/replay segments use exclusive new files and validated diagnostic manifests, not save/config modification.
- Original3DS baseline, 2D fallback, optional New3DS enhancements labeled. Real SDK APIs and cleanup/error paths, no fake sensors/audio/network/benchmarks.
- No holdout/scenario reading during implementation. All tests explain why; never weaken/skip/delete existing tests. Ordinary source smoke tests remain as regression signal.
- Loop alone commits: stage only exact OWNED individual files before finish; emit WGM_OWNERSHIP_MANIFEST. No git commit/stash/reset, jj new/rebase/switch or pinned branch moves. LC_ALL=C avoids mixed guard sorting.
- Leave code candidate in_progress for coordinator exact-pin RUGGED, independent spec/quality PASS, API/demo and final docs audit. No push/merge/remotes in author lane. Keep complete execution evidence and source checkpoints in durable project storage.
