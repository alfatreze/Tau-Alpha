# 004 — Detached Quartus launch exits before compilation

**Status:** Workaround active; root cause not investigated

## Context

Tau's Quartus build runs in the project Ubuntu x86-64 VM. The source is shared
from macOS through a 9p mount, but Quartus compiles only from the disposable
local ext4 copy at `/home/taualpha/tau-local/tau-alpha` because it cannot create
its database reliably on the shared mount. See `docs/FPGA_BUILD.md`.

## Observed behaviour

On 13 September 2026, attempts to start `make fpga` through SSH using `nohup`
or `setsid` returned a background PID but produced neither a Quartus process
nor the requested log file. The same checkout passed `make check-fpga`.

The failure happened before Quartus emitted its normal banner, so it is a
remote-session/detachment problem rather than an RTL or Quartus compilation
failure.

## Safe workaround

Run the build in a managed interactive SSH session and keep that terminal
session alive while it compiles:

```sh
cd /home/taualpha/tau-local/tau-alpha
QUARTUS_SH=/home/taualpha/intelFPGA_lite/25.1std/quartus/bin/quartus_sh make fpga
```

The Phase 1 SDRAM integration compile was started successfully this way. Its
initial Quartus synthesis banner was observed; its final fit/timing result was
not yet available when this note was written.

## Impact

- The build is not currently a fire-and-forget background job.
- A Codex task or user terminal that owns the managed SSH session must remain
  active for the compile duration.
- This does not affect the reproducibility of the build inputs or results.

## Next investigation

Determine whether UTM's guest SSH daemon, the PTY allocation, or the command
wrapper kills child processes on connection close. Prefer a verified solution
such as `tmux` or a system service only after confirming it leaves Quartus and
its report files intact.
