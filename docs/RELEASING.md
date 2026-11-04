# Releasing 3ds_foldquest

## Audited prior state versus this setup commit

- Repository: https://github.com/lafiabrew/3ds_foldquest
- Audited public-main source (before metadata setup): `2e58cf435f3e6b2c4c8d1da003ca2d92b3e5ad87`
- Actual current version tag: `untagged`
- Initial recommendation **only after qualification**: v0.1.0
- Channel: `wip` (existing public stable is not QA approval)
- Eligibility: blocked; initial policy `publish_enabled:false`.
- Prior audit CI configured: `false`.
- **After this commit is installed on public main: CI configured = true.**
  Merely rendering this document does not install it.
- Prior ledger CI executed: `false`; this setup does not establish a new CI run.
- Independent app review recorded in prior ledger: `false`; not inherited by a changed source.
- Existing release published: `false`; preserved, not certified by setup.
- Physical FBI/device evidence: not observed. No new physical test is claimed.

### Exact current blockers

- Public source is WIP; no existing stable CIA release
- No qualified exact-source app coordinator approval received
- Current exact-source license/history/asset provenance clearance not supplied
- Exact-source RUGGED + independent spec/quality and global Drop integration acceptance not supplied
- Release workflow not yet installed/executed; publisher not verified on this repository
- Reachable opaque-generator history unresolved; FoldQuest public ancestry must not be rewritten

## Setup and nonpublishing verification

Install exactly the workflow, policy and this document through the coordinator's
reviewed metadata rollout. Never copy a mutable kit pin. From the clean app clone:

```sh
make test &&
python3 -m unittest discover -s tests -p test_contract.py -v &&
make 3ds-native &&
make verify-3ds
```

The shared kit additionally requires regular nonempty tests/test_contract.py, collects at least one test, and rejects failures or skips; bare unittest discovery exit 0 with zero tests is not acceptance.

WIP stubs may deliberately fail; target availability is not successful compilation.
Record full commands, outputs, exits, source and binary hashes. Do not repair app
runtime as part of metadata rollout. The pinned reusable kit runs these policy
commands, validates native identity and keeps app build/test jobs read-only.

A real manual dry-run after installation (check its actual run ID/source/conclusion):

```sh
gh workflow run homebrew-release.yml --repo lafiabrew/3ds_foldquest --ref main -f dry_run=true &&
gh run list --repo lafiabrew/3ds_foldquest --workflow homebrew-release.yml --limit 5 --json databaseId,headSha,status,conclusion
```

Select the exact new run ID, then `gh run watch RUN_ID --repo lafiabrew/3ds_foldquest --exit-status`.
No external application-binary upload or publication in dry-run. Empty release_tag
builds a candidate; default dry_run=true and empty approval_id/ref cannot publish.
Tag pushes are also nonpublishing by default; they do not carry manual approvals.

## Qualified immutable release (coordinator only)

1. Preserve existing tags/assets, including legacy lightweight tags. For untagged
   sources the first recommendation is **v0.1.0**, not blanket v1.0.0. For existing
   versions inspect actual tags and reviewed changes before choosing next SemVer:
   `git ls-remote --tags origin`. Never reuse/move an existing tag.
2. Clear all app tests, independent RUGGED then fresh spec/quality gates (every
   required atom >=95), license/history/assets and any Drop producer freshness
   gate. Unknown banners or missing lawful game data remain blockers.
3. Enable policy only in a separately reviewed qualified source commit A0; keep
   executable caller/kit pinned identically to full immutable K0. Freeze A0 and
   obtain app-coordinator approval for that **exact** source/policy/tag/native
   identity and approved asset hashes. Any source change requires requalification.
4. Commit independently reviewed approval **data** in public hub main-reachable
   R1, with SHA256 record ID. R1 is separate from executable K0: do not repin A0
   and create an approval/source cycle. No code from approval_ref is executed.
5. Set NEW_TAG, APPROVED_SOURCE, APPROVAL_REF and APPROVAL_ID to the verified
   version/full A0/full R1/record digest. Check actual remote main is still A0;
   backup Git refs and export device saves with Checkpoint before installation.
   Only the authorized coordinator performs external writes:

```sh
git tag -a "$NEW_TAG" "$APPROVED_SOURCE" -m "3ds_foldquest $NEW_TAG" &&
git push origin "refs/tags/$NEW_TAG" &&
git ls-remote origin "refs/tags/$NEW_TAG" "refs/tags/$NEW_TAG^{}" &&
gh workflow run homebrew-release.yml --repo lafiabrew/3ds_foldquest --ref "$NEW_TAG" -f dry_run=false -f release_tag="$NEW_TAG" -f approval_ref="$APPROVAL_REF" -f approval_id="$APPROVAL_ID"
```

Verify the annotated tag object and peeled A0, exact new workflow run and green
publisher conclusion. The isolated publisher must independently validate enabled
stable policy, trusted approval, existing tag/source, native identity and hashes.
Expected immutable assets: `3ds_foldquest.cia`, optional matching `.3dsx`/`.smdh`,
`SHA256SUMS`, `release.json`. Download and verify their sizes/digests/source/tag;
never overwrite/clobber tags, releases or assets. Failed gates stop publication.

## Hub refresh, FBI install and update

The hub discovers releases hourly at minute 17 (eventual, not instantaneous).
Coordinator may run `gh workflow run pages.yml --repo lafiabrew/3ds_hub --ref main`,
watch the exact Pages run and probe the canonical site. Confirm card href equals
decoded QR equals the immutable versioned direct CIA URL, not release HTML.

Open https://lafiabrew.github.io/3ds_hub/ on another screen. On a CFW 3DS, back up saves first;
in FBI choose Remote Install / Scan QR Code and inspect the app/version before
accepting. Published HTTP/QR checks do **not** prove camera, HTTPS or installation
on physical hardware. Record device results separately. Update only to a newly
qualified immutable version; keep the prior CIA/checksum/save backup available.

## Recovery and rollback

Before metadata work, save a local backup branch at the clean original SHA and
record exact remote heads. Conflicts/remote races halt: preserve both histories,
never force/reset/stash/delete. For accepted metadata regressions propose a normal
reviewed revert commit; do not silently discard work. Reverting metadata does not
undo an immutable published release. Do not move old tags or replace old assets.
For device rollback use only the verified prior version after compatibility review
and a fresh save backup; save schemas may not be backwards-compatible. Restore
saves deliberately with Checkpoint, not by overwriting SD files blindly.

## Canonical references

- Releases: https://github.com/lafiabrew/3ds_foldquest/releases
- Kit/source and qualification contract: https://github.com/lafiabrew/3ds_hub/blob/60269b3fd15e8d5099ed18f20a927443e4eb3ba7/docs/RELEASING.md
- Rollout operations: https://github.com/lafiabrew/3ds_hub/blob/60269b3fd15e8d5099ed18f20a927443e4eb3ba7/docs/RELEASE_ROLLOUT.md
- FBI hub: https://lafiabrew.github.io/3ds_hub/
