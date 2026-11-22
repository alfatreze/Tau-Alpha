# Sharing a meter

A meter is independent: **nobody's approval is needed.** Build the package, put it anywhere, tell people.

## What to publish
`dist/<id>.tmeter` is the whole thing (the package already contains your `README.md`, `LICENSE`, factory presets and the `source/` copy). Say in your README that the presets are only offered once their folder is on the card (a composing tool such as Tau Omega copies it; by hand: [ON_A_POCKET.md](ON_A_POCKET.md) step 3b). A GitHub release layout that works:
```
release v1.2.0                       tag = the "version" in meter.json
  <id>.tmeter                        the package, exactly as tau-meter build made it
  SHA256SUMS.txt                     sha256 of the package (shasum -a 256 <id>.tmeter)
  preview.png                        optional: tau-meter test --preview
README.md of the repository          what it looks like, what it needs (abi, tau_min), the licence, how to install
```
Publish the **source** in the same repository and set `homepage` and `source` in `meter.json` to it: a meter without source is shown as "binary only" and people trust it less. Keep the **id** the same across versions of the same meter and raise `version`; a fork takes a new id ([IDS_AND_VERSIONS.md](IDS_AND_VERSIONS.md)). Run the full `tau-meter test` (and `--previous` the last release) before every release; put the output in the release notes if you like.

## Where it can live
Any URL: a file on a web page, a GitHub release, a message. A user installs from a file; a composing tool may fetch it from a URL or repository the user gives it. Tau Omega (the companion app) is the **planned** composer of the user bin and is described by its own project; until it handles meters, people compose by hand with `tau-meter compose` ([ON_A_POCKET.md](ON_A_POCKET.md)).

## What users will be told
The installing tool is meant to show your author, source, licence, the hashes and the line "a meter runs with full access to the player", and to label your meter **independent: not reviewed**. An update from the same source is shown and confirmed, never silent. That is the trust model ([TRUST.md](TRUST.md)): honest labelling, not a gate.

## Later, not built
- A **verified catalogue** (`tau-alpha-meters`): meters submitted as source, built by CI, run through this suite, reviewed, listed in a signed index. A design note for its index is in the collection repository; no catalogue exists yet and none is promised for a date.
- **Signing** of single meters (`signature` in the manifest is reserved and must be null today).
