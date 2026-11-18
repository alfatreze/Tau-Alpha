# Ids, versions, the ABI and `params_rev`

## Ids
```
tau-meter id new        # 9f3c1e7a52b84d06a1c9e34b7d20f518
```
An id is **128 random bits as 32 lower-case hex digits**, made once for each meter and never changed. No one keeps a registry and no naming rules exist: two authors cannot clash by accident. The id is the meter's identity for everything that follows it: the saved choice of meter, the user's tuning, the preset folder (the first 8 hex digits).
- The player keys on the first 32 bits, the *id hash*. Two meters whose hashes are equal cannot be in one bin (the composer refuses); two that share the low 16 bits run but the player will not save tuning for them (the composer warns).
- **A random id is not a proof of identity:** anyone can copy your id into their package. The composing tool treats an update as an update only when the source is the same; the same id from another source is a different meter to the user.
- **A fork must take a new id** (`tau-meter id new`), not keep the original's.
- The shipped meters have fixed ids (the collection lists them).

## Versions
- `version` is the meter's own semantic version (`major.minor.patch`); bump it for every release you publish. It is shown in the Info page and the list.
- `abi` is the host-table version the meter **needs** (`"1.0"`, `"1.1"`, `"1.2"`). The player supports exactly one major; a minor only ever appends (a new host-table entry or `mtr_in_t` field), so a meter built for 1.0 runs on a player that speaks 1.2. A meter that needs more than the player offers is refused and greyed in the list ("needs newer TAU"); the player never runs it half-informed. Declare the **lowest** minor that is true.
- A **major** ABI break strands meters; it is the player project's decision, announced beforehand. Tau Omega's planned guard names the meters that would be stranded before it installs a newer Tau.
- `tau_min` is the oldest Tau that can run the meter; leave it at `"0.7"` unless you depend on something newer.
- `params_rev` is separate: it versions the **parameter set** (see [PARAMETERS.md](PARAMETERS.md)).

## Which numbers the SDK enforces
`abi` in `meter.json` = the minor in the pack header (the build writes both). The suite's dynamic run offers your meter exactly the host table of the ABI you declared: a call to an entry from a later minor jumps to a null pointer and fails as `RUN-ABI-CALL`; a read of an `mtr_in_t` field from a later minor fails as `RUN-ABI-FIELDS`.
