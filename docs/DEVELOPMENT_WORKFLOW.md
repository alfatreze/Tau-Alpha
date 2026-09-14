# Tau Alpha conversational development workflow

## Roles and control flow

Codex owns planning, repository inspection, test selection, and review. The
local Qwen model only writes a proposed patch; it receives no file, shell,
network, or MCP tools. The deterministic Director validates/applies that
allowlisted patch and runs local gates. Claude is the final independent,
read-only auditor.

1. Codex Luna makes a read-only plan, reads project instructions and relevant
   source/tests, and emits a compact task packet plus an explicit file allowlist.
   For Figma tasks, Codex first makes a read-only brief through the official
   Figma MCP and includes it in the plan.
2. Qwen2.5-Coder-7B receives only the task packet and returns unified-diff text.
   The Director checks its diff paths against Codex's allowlist, runs
   `git apply --check`, and applies that exact patch itself.
3. Verilator lint and targeted cached Icarus tests run locally. Unknown RTL
   dependencies fall back to the complete Icarus RTL suite; non-RTL changes
   also run the fast host checks.
4. Codex Luna reviews the changed code read-only. If there are actionable
   findings, Codex assesses them against the task and creates one bounded
   repair packet. Qwen may propose one repair patch; Codex applies it, the gates
   rerun, and Codex reviews once more. Remaining findings stop the workflow.
5. Claude performs the final read-only audit. The workflow then stops. A human
   must explicitly approve any remote Quartus run.

Run the conversational surface with:

```sh
tools/triad/start_local_stack.sh
```

Then run a task from the repository root:

```sh
python3 tools/triad/director.py doctor
python3 tools/triad/director.py run "describe the requested change"
```

For a Figma task, include its URL in the request. The Director does not launch
Quartus, SSH, or change accounts. If Claude is not signed in, `run` exits before
any project file is changed. Smaller stages remain available as `plan`,
`develop`, `gate`, `review`, and `audit`; `develop` expects a preceding plan.

## Local services and permissions

The local model is `qwen2.5-coder-7b-instruct`, Q5_K_M, loaded by LM Studio
with a requested context of 16,000 tokens (aligned by LM Studio to 16,128) and
Flash Attention. The 14B models remain downloaded but unloaded because they
previously competed with Quartus for resources. The Director prefers GPT-5.6
Luna or Terra at low reasoning, with Luna as the default. Selecting a different
model (including Astra) or `xhigh`, `max`, or `ultra` reasoning requires
interactive approval before Codex launches. The approval covers the full `run`
up front, including a separate reviewer model; non-interactive runs stop rather
than bypassing the prompt. Override `TAU_CODEX_MODEL`,
`TAU_CODEX_REVIEW_MODEL`, or `TAU_CODEX_EFFORT` deliberately. Codex subscription
access is used; the Director does not use or store an OpenAI API key.

The stack binds LM Studio, UniClaudeProxy, and Computer to loopback:

- LM Studio: `127.0.0.1:1234`
- UniClaudeProxy: `127.0.0.1:9223`
- Computer: `127.0.0.1:8000`

Computer is installed natively and therefore has the macOS user's permissions.
Keep it loopback-only. Its state and Python environment are local ignored
directories `.cptr-data/` and `.venv-cptr/`.

Complete the Computer local-admin signup in the browser, then set up the
workspace and agent profiles in its settings. Add Codex at
`/Users/abel.santos/.local/bin/codex`, Claude Code at
`/usr/local/bin/claude`, and LM Studio at `http://127.0.0.1:1234/v1`. The
LM Studio key field is a placeholder only; it is not a paid API credential.
The Claude CLI must be signed in before the full triad can run. Claude's Figma
MCP login is separate from Claude CLI subscription authentication.

Codex and Claude connect directly to the official remote Figma MCP at
`https://mcp.figma.com/mcp`. The Figma brief enables only read tools. Qwen has
no MCP access.

## Tooling and HDL fast path

The Icarus test targets cache compiled `.vvp` outputs under `build/rtl/`:
`test-rtl-fb`, `test-rtl-tgt`, `test-rtl-eq`, `test-rtl-eq-cycles`,
`test-rtl-pcm`, `test-rtl-sdram-arbiter`, and `test-rtl-sdram-bridge`.
The `test-host` target includes plan/path allowlist regression checks for the
Director. `make rtl-lint` runs Verilator on the selected RTL. Lint warnings are a review
baseline, not functional signoff. Icarus provides 4-state behavioral
simulation. cocotb is optional and should be added only where Python stimulus
or assertions reduce test complexity; it does not inherently shorten HDL
compilation.

Quartus runs on the Ubuntu machine over SSH and uses its local ext4 project
copy. No local simulation claims Quartus fit/timing success. Any future remote
launcher must be detached, preserve an explicit status/log, and require human
approval before submission.

## Known issues and fixes

- Passing the full Figma and Claude MCP catalog to a 16k local model produced
  an initial prompt around 23.6k tokens. Qwen now receives only a short Codex
  packet, with no tool schemas.
- The original Qwen2.5 GGUF sometimes emitted an incomplete tool-call wrapper
  and once returned an incorrect heading after a read. The patch-only route
  removes tool-call parsing and makes Codex check/apply every diff, followed by
  deterministic tests and independent reviews.
- Qwen's old developer harness offered shell and file tools. The Director now
  runs it with `--tools ""` and strict empty MCP config. The model returns only
  patch text; the Director validates paths and applies the exact checked patch.
- If Qwen returns `NO_CHANGE` after Codex planned a patch, the Director stops
  before edits; Codex must revise or cancel that plan explicitly.
- A previous debug log stored the first 200 prompt characters. Logging now
  records lengths by default; previews require an explicit opt-in environment
  setting. This was a local log-privacy issue, not an LM Studio billing key.
- The `api_key` value in UniClaudeProxy is a dummy placeholder for local LM
  Studio compatibility, not a real secret or cloud credential.
- The 14B local model increased resource pressure alongside Quartus. Keep the
  7B model loaded for this flow and use the short 16k context.

## Validation state

The complete local Icarus suite, Director plan/patch safety tests, host checks,
and Verilator lint pass, with pre-existing nonfatal lint warnings documented in
the audit trail. The live triad has not yet run because Claude CLI sign-in and
Computer account/profile setup remain for the user. Neither action should be
inferred from the Figma OAuth connection.

Generated plans, patches, reviews, and logs are under ignored `work/triad/`.
