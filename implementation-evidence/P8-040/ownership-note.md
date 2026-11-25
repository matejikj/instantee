# Ownership / lifetime / crash boundaries

## Staging ownership

`OutputTransaction` owns a private `.instantee-txn-*` directory beside the
target, locked via an exclusive flock on `.instantee-output-txn`. The destructor
only removes unpublished staging; it never renames and never deletes a published
target.

## Publish linearization

File sync runs before rename. Rename is the publication linearization point
(`published=true`). Directory metadata sync follows; failure yields durability
`unknown` with a post-publish error while keeping the visible MP4.

## Crash harness barriers

Test-only `phase_barrier` parks at `file_sync`, `rename`, `metadata_sync`, and
`report`. The child also parks at `finish` (staging written, commit not started)
and `published` (commit returned). Parent SIGKILL observes the contract table
without claiming power-loss durability.

## Concurrency

Concurrent no-replace publish remains covered by the existing transaction unit
test (one winner, one `publication_conflict`). Crash matrix uses overwrite so
an existing previous target stays intact until rename.
