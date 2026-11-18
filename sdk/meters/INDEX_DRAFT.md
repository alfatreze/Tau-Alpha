# `index.json`: DRAFT of the catalogue index (a design note, nothing is built)

The optional **verified tier** of the meter ecosystem (decision D-M23): meters submitted as **source** by pull request, built by CI from that source, run through the conformance suite, reviewed, and listed in a **signed** index. Independent meters (anyone's file, URL or repository) need none of this. Opening submissions is not part of release 0.7.5.

## Layout of this repository for a catalogue
```
meters/<name>/              one SDK project per meter (meter.json, src/, thumb.json, presets.json, LICENSE, README.md)
dist/<id>.tmeter            built by CI from the source, never uploaded by an author
index.json                  the signed index
index.json.sig              detached signature
SHA256SUMS.txt
```
## `index.json` (draft)
```
{ "schema": 1,
  "key_id": "1a2b3c4d",                          first 8 hex digits of the SHA-256 of the 32-byte public key
  "generated": "2026-11-18T12:00:00Z",
  "meters": [
    { "id": "143acb80cbcd1b08b84c2661eeab3414",   32 hex digits
      "name": "WINAMP SCOPE", "version": "1.0.0", "abi": "1.0", "tau_min": "0.0", "params_rev": 1,
      "package": "dist/143acb80cbcd1b08b84c2661eeab3414.tmeter",
      "sha256": "<of the package>", "size": 38199,
      "author": "alfatreze", "licence": "MIT",
      "source": "https://github.com/alfatreze/tau-alpha-meters/tree/<commit>/meters/winamp_scope",
      "commit": "<the commit CI built>" } ],
  "revoked": [ { "id": "...", "version": "1.0.0", "reason": "plain words" } ] }
```
## Signing (from `EXTERNAL_METERS_SPEC.md` section 6a, text only)
- **What is signed:** the bytes of `index.json` exactly as published (UTF-8, no re-serialising): there is no canonical form to agree on; one changed byte invalidates the signature.
- **Signature and key:** ed25519 (RFC 8032); `index.json.sig` holds the 64 raw signature bytes as one line of base64; the public key is 32 raw bytes compiled into the composing tool (written as 64 hex digits in documents).
- **Key id and rotation:** `key_id` names the key; the tool carries the current and the next key so a rotation ships before the index switches; an unknown `key_id` makes catalogue meters *unverifiable* (not blocked: an installed meter keeps working).
- **"Verified" means exactly** "the signed index lists this exact package hash (and version)", nothing more; no screen says "safe".
- **`revoked`** applies to verified meters only; the tool shows them and offers to hide or remove them, never removes one by itself. An independent meter is never revoked by the project.
`index.draft.json` next to this file is an example made from the seven shipped meters (unsigned).
