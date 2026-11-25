# Reusable local agent workflow for HDL projects

This guide captures a reusable, low-cost setup for conversational software and
FPGA development on an Apple Silicon Mac, with a remote Quartus machine. Adapt
the project paths, test map, tool versions, and model size to each repository.

## Intended architecture

```text
User (conversation UI)
          |
          v
Codex planner / tool owner / reviewer
          | compact task packet              ^ tests + review
          v                                   |
Local Qwen coder -- unified diff only --> Director allowlist + apply + gates
                                             |
                                             v
                                 Claude read-only final auditor
                                             |
                                             v
                       Human decides whether to launch remote Quartus
```

Codex owns repository inspection, Figma/MCP calls, task decomposition, edits,
commands, tests, and review decisions. The local model has no file, shell,
network, or MCP tools; it only proposes a diff from a bounded packet. This
keeps local inference inexpensive and avoids depending on small-model tool-call
parsing. Claude independently audits the result and evidence when its CLI is
authenticated.

## Cost boundary

LM Studio, MLX, Icarus Verilog, Verilator, cocotb, Computer, and a local
orchestrator can run without per-token API charges. Configure Codex and Claude
through their subscription-backed CLI/app authentication if those subscriptions
are already available. Do not add API keys to project scripts or the Director.
Subscription usage limits still apply; confirm the active account and billing
settings before enabling any paid extra usage. A paid Figma seat remains subject
to the plan's own MCP limits.

For day-to-day Codex orchestration, choose its least costly suitable model and
reasoning level. GPT-5.6 Luna and Terra are preferred here, with Luna as the
default for routine planning and review. The Director asks for interactive
approval before using any other model or `xhigh`, `max`, or `ultra` reasoning;
non-interactive runs fail closed. Keep every cloud-backed step visible in the
run log and avoid background polling loops that burn subscription usage.

## Mac model and serving layer

1. Start with one coder-specialized model that fits beside the user's other
   workloads. This setup uses Qwen2.5-Coder-7B-Instruct GGUF, Q5_K_M, at about
   16k context in LM Studio. Keep larger models unloaded when Quartus or other
   memory-heavy applications run.
2. Bind the local model server to `127.0.0.1`; avoid LAN exposure by default.
3. If you want Apple-Silicon-native MLX later, A/B-test an MLX quantization of
   the same model on representative tasks before replacing the known-good
   model. Measure patch acceptance, targeted tests, response latency, and peak
   memory while Quartus is active. Changing GGUF to MLX improves execution
   compatibility/performance, not the underlying model's code knowledge.
4. Give the local model one system instruction: return a unified diff only,
   touching paths explicitly listed by Codex. Do not supply tools or MCP
   schemas. Keep its packet and source excerpts small enough to leave room for
   generated patches inside the configured context.

LM Studio's local-compatible API may still show an API-key field. A dummy
placeholder can satisfy a local client that requires a non-empty value; it is
not a secret and does not authorize cloud usage. Keep real credentials out of
repository files and logs.

## One-time workstation setup

For a fresh macOS host, install the conversation surface and required local
agent dependency in a dedicated environment. Computer's current installation
requires Python 3.10 or newer; the official instructions are linked below.

```sh
python3 -m venv .venv-computer
.venv-computer/bin/python -m pip install 'cptr[mcp]' claude-agent-sdk
.venv-computer/bin/cptr run --host 127.0.0.1 --port 8000
```

Create the local Computer administrator through its one-time setup page, open
the project workspace, and add Codex and Claude Code under Settings → Admin →
Agents. Install and authenticate both CLIs on the same Mac first; Computer
reuses their existing subscription sessions. The Claude profile additionally
needs `claude-agent-sdk` in Computer's Python environment. Add LM Studio as a
local model connection only if the UI needs direct model selection; the
Director's patch-only Qwen route uses UniClaudeProxy locally. Check each
profile's status is `ready` before using it.

Install/launch the local model through the LM Studio app/CLI. For this project's
model ID, the reproducible command sequence is:

```sh
lms server start --port 1234
lms load qwen2.5-coder-7b-instruct \
  --identifier qwen2.5-coder-7b-instruct \
  --context-length 16000 \
  --gpu auto
```

The 16,000 request may be aligned by LM Studio to 16,128. Confirm the loaded
instance and effective context through LM Studio before running the Director.
Keep model server, proxy, and Computer bound to `127.0.0.1` unless a separate
network-security design is made.

If design context is needed, connect Figma directly to each agent account that
will use it, then authenticate in that client. The official remote endpoint and
manual CLI commands are:

```sh
codex mcp add figma --url https://mcp.figma.com/mcp
claude mcp add --scope user --transport http figma https://mcp.figma.com/mcp
claude mcp login figma
```

Use Codex's project workflow to make read-only design briefs; keep the Figma
tools away from the small local coder. See the [Computer install guide](https://docs.openwebui.com/ecosystem/computer/install/),
[Computer agent profiles](https://docs.openwebui.com/ecosystem/computer/ai/coding-agents/),
and [Figma remote MCP setup](https://developers.figma.com/docs/figma-mcp-server/remote-server-installation/)
for the supported setup flow and any version-specific changes.

## Conversation UI and orchestration

Choose a conversation-first UI that can launch the installed Codex and Claude
agents, and can run the local orchestrator in a project workspace. Open WebUI
Computer is used here. Native Computer inherits the macOS user's permissions,
so bind it to loopback and explicitly scope the workspace. Create its local
admin account yourself, then configure the Codex CLI, Claude CLI, and LM Studio
connections. Test every profile with a read-only command before allowing edits.

Use a deterministic project-local Director rather than relying on a generic
chat model to remember the workflow. Store prompts, path allowlists, test
mappings, and ignored run reports with the project. The Director should:

1. Check local service health and required logins before starting.
2. Ask Codex for a structured, read-only plan containing acceptance criteria,
   minimal file excerpts, exact local tests, and allowed paths.
3. Ask the local coder for a unified diff, with all tools disabled.
4. Validate diff syntax and allowed paths, then apply the exact patch
   deterministically in the Director. Do not give the model an unrestricted
   write session solely to apply generated diff text.
5. Run quick local tests and lint before slower integration tests.
6. Run a read-only adversarial Codex review. Let Codex assess findings and
   direct at most one bounded repair pass; fail closed if findings remain.
7. Run a read-only Claude audit, then stop for human approval before remote
   synthesis, programming hardware, or release actions.

Figma MCP, when used, should connect directly to the official server under the
user's own account. Let Codex make a read-only design brief and pass only the
necessary details to the local model. Never dump the full MCP catalog into a
small model's prompt. Verify which Figma team/seat is active because read
limits may differ by seat.

## HDL fast feedback before Quartus

Use a short-to-long verification ladder:

1. Format, static checks, and Verilator lint on changed modules.
2. Targeted Icarus Verilog tests for the changed behavior. Cache compiled
   simulator outputs and rebuild only when HDL or test dependencies change.
3. Full local RTL regression when dependencies are uncertain or before review.
4. cocotb only where Python stimulus, scoreboards, or reusable assertions make
   a test simpler; cocotb does not inherently make HDL compilation faster.
5. Quartus compile/fit/timing on the remote machine after a human-reviewed
   local result. Keep SSH launch detached with a unique run ID, durable logs,
   explicit status polling, and no automatic submission from a model.

Map each RTL module and testbench to its fast test target. Unknown changed RTL
should trigger the full suite rather than silently skipping tests. Label results
accurately: Verilator is lint; Icarus is 4-state simulation; neither proves
Quartus fitting, timing closure, or hardware behavior.

## Troubleshooting log

| Symptom | Cause | Fix or guardrail |
|---|---|---|
| Initial prompt exceeds local context | Full MCP/tool definitions and chat history crowd out source code | Keep tools disabled for local Qwen; let Codex prepare compact excerpts and a task packet |
| Local model describes intended edits but does not make them | The model was expected to control tools or tool syntax failed | Make it a patch-only worker; validate its diff and have the Director apply the exact diff |
| Local model returns `NO_CHANGE` for a planned patch | Its packet may be insufficient or it judges the task needs no edit | Stop before applying anything; have Codex revise the task packet or cancel the task |
| Model emits partial XML/tool-call markup | Model template and harness parser disagree | Remove the tool-call dependency instead of broadening a permissive parser |
| Patch changes unrelated paths | Model receives broad repository context or no explicit scope | Include an allowlist from Codex; reject every unlisted diff path before application |
| Larger local model slows Quartus or causes memory pressure | Both workloads compete for unified memory | Unload larger models, use a smaller quantization/context, and test peak memory while Quartus runs |
| RTL edit waits for a long Quartus compile | The edit has no fast local target or integration is run too early | Add targeted Icarus tests and lint; reserve Quartus for implementation/timing evidence |
| A log unexpectedly contains prompt text | Debug mode stores text previews (for example, the first 200 characters) | Log lengths/metadata by default; make preview logging explicit opt-in and keep logs local |
| Local API configuration appears to need a key | OpenAI-compatible local clients may require a non-empty field | Use a documented dummy local placeholder only; never place a cloud key there |
| Figma/MCP tools inflate prompt or exceed limits | Every enabled tool schema consumes context and seats may have different limits | Enable only read tools needed for the brief, use the right seat, and keep MCP away from Qwen |
| Full run stops at Claude | Claude CLI auth is separate from the Figma OAuth connection | Authenticate the CLI and confirm any plan/usage setting before the run; never assume Figma login covers it |
| Lint reports warnings while tests pass | Lint and simulation catch different classes of issue | Track warnings as a baseline; review new warnings and don't call lint a functional pass |

## Project onboarding checklist

- [ ] Record host/OS, memory size, local model file/quantization/context, and
      which applications share memory with inference.
- [ ] Install and test local LM Studio/MLX server on loopback.
- [ ] Install the conversation UI and create its local admin account.
- [ ] Add Codex/Claude profiles; verify subscription auth separately from MCP
      OAuth and check cost/usage preferences.
- [ ] Connect Figma MCP directly if needed; identify the permitted team/seat.
- [ ] Add project instructions that specify protected files, hardware evidence
      rules, test conventions, and audit/documentation requirements.
- [ ] Write concise role prompts and a structured Codex plan schema.
- [ ] Implement and test diff parsing, path allowlists, `git apply --check`,
      and a clean stop on malformed patches.
- [ ] Keep regression tests for plan validation and patch path checks in the
      fast host test target.
- [ ] Map changed RTL files to fast tests and define the unknown-file fallback.
- [ ] Run one disposable software-only task through plan, patch, apply, tests,
      review, bounded repair, and audit.
- [ ] Run one low-risk RTL task through Icarus and Verilator; inspect resource
      usage while Quartus is idle and then, if needed, active.
- [ ] Record setup/version, failures, fixes, test outputs, and remaining gates
      in a project guide and dated audit trail.
- [ ] Only then enable human-approved Quartus-over-SSH operation.

## Reuse notes

Copy this document and the `tools/triad` structure into another repository,
then replace absolute executable locations, model ID, service ports, test
mapping, project instructions, remote Quartus paths, and account-specific
Figma details. Keep generated plans, diffs, and logs ignored; do not copy live
tokens, API keys, cookies, or Computer account data into a new project.
