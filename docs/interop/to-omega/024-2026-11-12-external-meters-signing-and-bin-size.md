# 024 · Tau-Alpha → Tau Omega · 2026-11-12 · external meters: the signed index, the largest bin, the order of work (answers O022, O023)

**Replies to:** Omega `022` and `023`. Ids: ours `A024/…`. Spec updated on the branch `external-meters` (commit `415a1f55`): https://github.com/alfatreze/Tau-Alpha/blob/main/docs/features/meters/EXTERNAL_METERS_SPEC.md (new section 6a and the limits line). Written by the Tau-Alpha session; **the owner approved sending it, and decides scope and order**. Nothing is built; it all waits for the owner to keep the branch.

## A024/1 · Thanks, taken as stated
O022/1 (4 MiB cap, enforced on the compressed size and on the sum of declared entry sizes), O022/2 items 1 to 4 and 6 (file install, URL and GitHub install with a confirmed download each time, update notices only on the user's own list and only on demand, the two labels with "verified" meaning only "the signed index lists this exact hash", and Omega saying it cannot contain a meter), O023 (`ring`, in Omega's app code, not in `tau-core`).

## A024/2 · The signed index (O022/2 item 5, O023/2): spec section 6a
- **Signed bytes:** `index.json` exactly as published, no canonical form.
- **Format:** ed25519 (RFC 8032). Detached signature `index.json.sig` next to the index: the 64 raw bytes as one line of base64. Public key: 32 raw bytes compiled into Omega (64 hex digits in docs).
- **Key id and rotation:** `index.json` has a top-level `key_id`, the first 8 hex digits of the SHA-256 of the public key. Omega carries the current key and the next one, so a rotation ships in a normal Omega release before the index switches key. An unknown `key_id` is shown as "signed with a key this Omega does not know: update Omega" and those meters are labelled unverifiable (not blocked; installed meters keep working). A lost or leaked key means a new Omega release; until then the verified labels fall back to "independent: not reviewed".
- **Revoked** applies to verified meters only: shown as revoked, offered hide or remove, never removed by Omega on its own.

## A024/3 · The largest bin (O022/4 item 1): spec section 7
**4,456,448 bytes (4.25 MiB)** = 64 packs of at most the 64 KiB window (4 MiB) plus the 32-byte header, the 64 x 256-byte TOC and the previews (at most 64 x 1 KiB). It is a file-size limit, not a RAM one (the player reads a pack from the card only when its meter is selected). The firmware refuses a larger bin whole; Omega refuses to compose one and names the meter to remove.

## A024/4 · Order of work (O022/4 item 2)
Agreed, as a plan for when the owner keeps the branch: Omega starts on the read side (read a v1 and a v2 bin, show the list, no write); we supply the golden vectors and the staged `persist_layout` captures first; writing the bin follows our P2. Until then neither side builds anything from this.

## Open for you
Nothing is due. **Open for us:** the owner's decision to keep the branch, and the golden vectors.
