#!/usr/bin/env python3
"""Codex-led Qwen/Codex/Claude workflow for bounded repository changes.

Codex plans and owns repository/tools, Qwen returns patch text only, local gates
verify it, Codex reviews and may direct one repair, and Claude audits. Quartus
is never launched by this program.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path, PureWindowsPath
import shlex
import shutil
import subprocess
import sys
import urllib.error
import urllib.request


ROOT = Path(__file__).resolve().parents[2]
TRIAD_DIR = ROOT / "work" / "triad"
PROMPTS = Path(__file__).resolve().parent / "prompts"
PLAN_SCHEMA = Path(__file__).resolve().parent / "schemas" / "codex-plan.schema.json"
CODEX = Path(os.environ.get("TAU_CODEX", "/Users/abel.santos/.local/bin/codex"))
CLAUDE = Path(os.environ.get("TAU_CLAUDE", "/usr/local/bin/claude"))
QWEN = Path(__file__).resolve().parent / "qwen_developer.sh"
CODEX_MODEL = os.environ.get("TAU_CODEX_MODEL", "gpt-5.6-luna")
CODEX_REVIEW_MODEL = os.environ.get("TAU_CODEX_REVIEW_MODEL", CODEX_MODEL)
CODEX_EFFORT = os.environ.get("TAU_CODEX_EFFORT", "low")
PREFERRED_CODEX_MODELS = {"gpt-5.6-luna", "gpt-5.6-terra"}
APPROVAL_REQUIRED_MODELS = {"gpt-6-astra"}
APPROVAL_REQUIRED_EFFORTS = {"xhigh", "extra-high", "max", "ultra"}


def run(
    cmd: list[str], *, input_text: str | None = None, capture: bool = False
) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(
        cmd, cwd=ROOT, input=input_text, text=True, capture_output=capture, check=False
    )


def fetch_json(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=5) as response:
        return json.load(response)


def claude_logged_in() -> bool:
    auth = run([str(CLAUDE), "auth", "status"], capture=True)
    if auth.returncode:
        return False
    try:
        return bool(json.loads(auth.stdout).get("loggedIn"))
    except (json.JSONDecodeError, AttributeError):
        return False


def codex_models_for_command(
    command: str,
    *,
    model: str = CODEX_MODEL,
    review_model: str = CODEX_REVIEW_MODEL,
) -> list[str]:
    if command in {"plan", "figma-brief"}:
        return [model]
    if command == "review":
        return [review_model]
    if command == "run":
        return [model, review_model]
    return []


def normalize_codex_effort(effort: str) -> str:
    normalized = effort.strip().lower().replace("_", "-").replace(" ", "-")
    return "xhigh" if normalized == "extra-high" else normalized


def codex_approval_reasons(
    command: str,
    *,
    model: str = CODEX_MODEL,
    review_model: str = CODEX_REVIEW_MODEL,
    effort: str = CODEX_EFFORT,
) -> list[str]:
    selected_models = codex_models_for_command(
        command, model=model, review_model=review_model
    )
    reasons = []
    for selected in dict.fromkeys(selected_models):
        normalized_model = selected.strip().lower()
        if normalized_model in APPROVAL_REQUIRED_MODELS:
            reasons.append(f"Astra model {selected}")
        elif normalized_model not in PREFERRED_CODEX_MODELS:
            reasons.append(f"non-preferred model {selected}")
    normalized_effort = normalize_codex_effort(effort)
    if selected_models and normalized_effort in APPROVAL_REQUIRED_EFFORTS:
        reasons.append(f"reasoning effort {normalized_effort}")
    return reasons


def confirm_codex_configuration(
    command: str,
    *,
    model: str = CODEX_MODEL,
    review_model: str = CODEX_REVIEW_MODEL,
    effort: str = CODEX_EFFORT,
) -> bool:
    reasons = codex_approval_reasons(
        command, model=model, review_model=review_model, effort=effort
    )
    if not reasons:
        return True
    print(
        "Codex approval required before launch: " + ", ".join(reasons)
        + ". This may use more of your Codex usage allowance."
    )
    if not sys.stdin.isatty():
        print("STOP: approval needs an interactive terminal; no Codex task launched.")
        return False
    try:
        answer = input("Proceed with this Codex configuration? [y/N] ")
    except EOFError:
        answer = ""
    if answer.strip().lower() not in {"y", "yes"}:
        print("STOP: Codex approval not granted; no Codex task launched.")
        return False
    return True


def doctor(_: argparse.Namespace) -> int:
    failures: list[str] = []
    for name, executable in (
        ("python", Path(sys.executable)), ("codex", CODEX),
        ("claude", CLAUDE), ("qwen wrapper", QWEN),
    ):
        ok = executable.exists()
        print(f"{'OK' if ok else 'FAIL'}  {name}: {executable}")
        if not ok:
            failures.append(name)

    for command in ("iverilog", "vvp", "verilator", "make", "git", "ssh"):
        path = shutil.which(command)
        print(f"{'OK' if path else 'FAIL'}  {command}: {path or 'not found'}")
        if not path:
            failures.append(command)

    try:
        models = fetch_json("http://127.0.0.1:1234/api/v1/models")["models"]
        qwen = next(model for model in models if model["key"] == "qwen2.5-coder-7b-instruct")
        instances = qwen.get("loaded_instances", [])
        context = instances[0]["config"]["context_length"] if instances else 0
        ok = context >= 16000
        print(f"{'OK' if ok else 'FAIL'}  LM Studio Qwen 7B loaded, context={context}")
        if not ok:
            failures.append("LM Studio Qwen context")
    except (OSError, ValueError, KeyError, StopIteration, urllib.error.URLError) as exc:
        print(f"FAIL  LM Studio: {exc}")
        failures.append("LM Studio")

    for name, url in (
        ("UniClaudeProxy", "http://127.0.0.1:9223/"),
        ("Computer", "http://127.0.0.1:8000/api/health"),
    ):
        try:
            health = fetch_json(url)
            ok = health.get("status") == "ok"
            print(f"{'OK' if ok else 'FAIL'}  {name}: {health}")
            if not ok:
                failures.append(name)
        except (OSError, ValueError, urllib.error.URLError) as exc:
            print(f"FAIL  {name}: {exc}")
            failures.append(name)

    logged_in = claude_logged_in()
    print(f"{'OK' if logged_in else 'FAIL'}  Claude auditor login")
    if not logged_in:
        failures.append("Claude auditor login")
    return 1 if failures else 0


RTL_TARGETS = {
    "src/fpga/core/mp3_fb.sv": {"test-rtl-fb"},
    "src/fpga/core/font_rom.v": {"test-rtl-fb"},
    "src/fpga/core/tgt_cmd.v": {"test-rtl-tgt"},
    "src/fpga/core/eq_biquad.v": {"test-rtl-eq", "test-rtl-eq-cycles"},
    "src/fpga/core/pcm_fifo.v": {"test-rtl-pcm"},
    "src/fpga/core/tau_sdram_arbiter.sv": {"test-rtl-sdram-arbiter"},
    "src/fpga/core/tau_sdram_cpu_bridge.sv": {"test-rtl-sdram-bridge"},
    "sim/tb_mp3_fb.v": {"test-rtl-fb"},
    "sim/tb_tgt_cmd.v": {"test-rtl-tgt"},
    "sim/tb_eq_biquad.v": {"test-rtl-eq"},
    "sim/tb_eq_cycles.v": {"test-rtl-eq-cycles"},
    "sim/tb_pcm_decay.v": {"test-rtl-pcm"},
    "sim/tb_tau_sdram_arbiter.v": {"test-rtl-sdram-arbiter"},
    "sim/tb_tau_sdram_cpu_bridge.v": {"test-rtl-sdram-bridge"},
}


def changed_files() -> list[str]:
    result = run(["git", "status", "--porcelain=v1"], capture=True)
    return [line[3:].split(" -> ")[-1] for line in result.stdout.splitlines() if len(line) >= 4]


def gate(_: argparse.Namespace) -> int:
    TRIAD_DIR.mkdir(parents=True, exist_ok=True)
    targets: set[str] = set()
    unknown_rtl = False
    paths = changed_files()
    has_rtl = any(path.startswith(("src/fpga/", "sim/")) for path in paths)
    has_non_rtl = any(not path.startswith(("src/fpga/", "sim/")) for path in paths)
    for path in paths:
        targets.update(RTL_TARGETS.get(path, set()))
        if path.startswith(("src/fpga/", "sim/")) and path not in RTL_TARGETS:
            unknown_rtl = True
    if unknown_rtl:
        targets = {"test-rtl"}
    elif not has_rtl:
        targets = {"test-host"}
    if has_non_rtl:
        targets.add("test-host")

    commands = [["make", "rtl-lint"], ["make", *sorted(targets)]]
    log_path = TRIAD_DIR / "gate.log"
    with log_path.open("w", encoding="utf-8") as log:
        for cmd in commands:
            result = subprocess.run(
                cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
            )
            log.write(f"$ {' '.join(cmd)}\n{result.stdout}\n")
            print(result.stdout, end="")
            if result.returncode:
                print(f"Gate failed; see {log_path}")
                return result.returncode
    print(f"Gate passed; log: {log_path}")
    return 0


def validate_plan(plan: dict) -> None:
    if plan.get("status") not in {"patch", "no_change"}:
        raise ValueError("status must be 'patch' or 'no_change'")
    packet = plan.get("task_packet")
    if not isinstance(packet, str) or not packet.strip():
        raise ValueError("task_packet must be a non-empty string")
    paths = plan.get("allowed_files")
    if not isinstance(paths, list):
        raise ValueError("allowed_files must be an array")
    if plan["status"] == "patch" and not paths:
        raise ValueError("a patch plan requires at least one allowed file")
    if plan["status"] == "no_change" and paths:
        raise ValueError("a no_change plan must have an empty allowed_files array")
    for value in paths:
        if not isinstance(value, str) or not value:
            raise ValueError(f"invalid allowed file: {value!r}")
        rel = Path(value)
        win_rel = PureWindowsPath(value)
        if (
            rel.is_absolute() or win_rel.is_absolute()
            or ".." in rel.parts or ".." in win_rel.parts
        ):
            raise ValueError(f"unsafe allowed file: {value!r}")
        if not (ROOT / rel).resolve().is_relative_to(ROOT.resolve()):
            raise ValueError(f"allowed file escapes repository: {value!r}")


def codex_plan(args: argparse.Namespace, *, repair_report: Path | None = None) -> int:
    TRIAD_DIR.mkdir(parents=True, exist_ok=True)
    report = TRIAD_DIR / ("codex-repair-plan.json" if repair_report else "codex-plan.json")
    instructions = (PROMPTS / "codex-planner.md").read_text(encoding="utf-8")
    context = ""
    if repair_report:
        context = (
            "\n\nPrior plan:\n" + (TRIAD_DIR / "codex-plan.json").read_text(encoding="utf-8")
            + "\n\nReview to assess and repair only if valid and in scope:\n"
            + repair_report.read_text(encoding="utf-8")
        )
    prompt = f"Task:\n{args.task}\n\n{instructions}{context}"
    cmd = [
        str(CODEX), "exec", "-C", str(ROOT), "-s", "read-only", "--ephemeral",
        "--ignore-user-config", "--output-schema", str(PLAN_SCHEMA),
        "-m", CODEX_MODEL, "-c", f'model_reasoning_effort="{normalize_codex_effort(CODEX_EFFORT)}"',
        "-o", str(report), "-",
    ]
    result = run(cmd, input_text=prompt)
    if result.returncode:
        return result.returncode
    try:
        validate_plan(json.loads(report.read_text(encoding="utf-8")))
    except (OSError, ValueError, TypeError) as exc:
        print(f"Codex plan invalid; stopping before Qwen: {exc}")
        return 2
    print(f"Codex plan: {report}")
    return 0


def validate_patch_paths(text: str, allowed_files: list[str]) -> set[str]:
    if not text.startswith("diff --git "):
        raise ValueError("expected a unified diff beginning with 'diff --git'")
    changed: set[str] = set()
    for line in text.splitlines():
        if line.startswith((
            "new file mode 120000", "old mode 120000", "new mode 120000",
            "GIT binary patch", "Binary files ",
        )):
            raise ValueError("binary and symlink patches are not accepted")
        if not line.startswith("diff --git "):
            continue
        try:
            parts = shlex.split(line)
        except ValueError as exc:
            raise ValueError("malformed diff header quoting") from exc
        if len(parts) != 4:
            raise ValueError("malformed diff header")
        for name in parts[2:]:
            if not name.startswith(("a/", "b/")):
                raise ValueError(f"unexpected patch path {name!r}")
            rel = name[2:]
            win_rel = PureWindowsPath(rel)
            posix_rel = Path(rel)
            if (
                posix_rel.is_absolute() or win_rel.is_absolute()
                or ".." in posix_rel.parts or ".." in win_rel.parts
            ):
                raise ValueError(f"unsafe patch path {rel!r}")
            changed.add(rel)
    if not changed or not changed <= set(allowed_files):
        raise ValueError(f"patch paths outside Codex allowlist: {sorted(changed - set(allowed_files))}")
    return changed


def qwen_declined_patch(text: str) -> bool:
    return text.strip() == "NO_CHANGE"


def apply_qwen_patch(patch_path: Path, allowed_files: list[str]) -> None:
    text = patch_path.read_text(encoding="utf-8")
    if text.startswith("```diff") and text.rstrip().endswith("```"):
        text = text[len("```diff"): text.rfind("```")].strip() + "\n"
        patch_path.write_text(text, encoding="utf-8")
    validate_patch_paths(text, allowed_files)
    subprocess.run(["git", "apply", "--check", str(patch_path)], cwd=ROOT, check=True)
    subprocess.run(["git", "apply", str(patch_path)], cwd=ROOT, check=True)


def qwen_patch(args: argparse.Namespace, *, repair: bool = False) -> int:
    TRIAD_DIR.mkdir(parents=True, exist_ok=True)
    plan_file = TRIAD_DIR / ("codex-repair-plan.json" if repair else "codex-plan.json")
    plan = json.loads(plan_file.read_text(encoding="utf-8"))
    validate_plan(plan)
    if plan["status"] == "no_change":
        print("Codex plan requires no changes; skipping Qwen.")
        return 0
    system_prompt = (PROMPTS / "qwen-developer.md").read_text(encoding="utf-8")
    prompt = f"Task: {args.task}\n\nCodex task packet:\n{plan['task_packet']}\n"
    prompt += "\nCodex-approved changed paths:\n" + "\n".join(
        f"- {path}" for path in plan["allowed_files"]
    ) + "\n"
    if repair:
        prompt += "\nCodex review findings:\n" + (TRIAD_DIR / "codex-review.md").read_text(encoding="utf-8")
    report = TRIAD_DIR / ("qwen-repair.patch" if repair else "qwen.patch")
    cmd = [
        str(QWEN), "-p", "--model", "claude-3-5-sonnet-20241022",
        "--system-prompt", system_prompt,
        "--strict-mcp-config", "--mcp-config", '{"mcpServers":{}}',
        "--restricted", "--tools", "", "--no-session-persistence",
        "--output-format", "text", prompt,
    ]
    result = run(cmd, capture=True)
    report.write_text(result.stdout, encoding="utf-8")
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    if result.returncode:
        return result.returncode
    if qwen_declined_patch(result.stdout):
        print(
            "Qwen returned NO_CHANGE; no files were changed. "
            "Codex must revise or cancel the plan."
        )
        return 3
    try:
        apply_qwen_patch(report, plan["allowed_files"])
    except (ValueError, subprocess.CalledProcessError) as exc:
        print(f"Qwen patch rejected; no changes applied: {exc}")
        return 2
    print(f"Qwen patch validated and applied by the Director from {report}")
    return 0


def design_brief(args: argparse.Namespace) -> int:
    TRIAD_DIR.mkdir(parents=True, exist_ok=True)
    report = TRIAD_DIR / "figma-brief.md"
    prompt = f"""Use only the official Figma MCP and read-only repository inspection.
First call Figma whoami to verify account access. Inspect the supplied Figma URL,
then create a compact implementation brief for the local Qwen patch author:
dimensions, layout, colors, typography, assets, states, relevant production
files, and concrete acceptance criteria. Do not edit Figma or repository files.
URL/task: {args.task}
"""
    cmd = [
        str(CODEX), "exec", "-C", str(ROOT), "-s", "read-only", "--ephemeral",
        "--ignore-user-config",
        "-c", 'mcp_servers.figma.url="https://mcp.figma.com/mcp"',
        "-c", 'mcp_servers.figma.enabled_tools=["whoami","get_design_context","get_screenshot","get_metadata","get_variable_defs"]',
        "-m", CODEX_MODEL, "-c", f'model_reasoning_effort="{normalize_codex_effort(CODEX_EFFORT)}"',
        "-o", str(report), "-",
    ]
    result = run(cmd, input_text=prompt)
    if result.returncode == 0:
        print(f"Figma brief: {report}")
    return result.returncode


def codex_review(args: argparse.Namespace) -> int:
    TRIAD_DIR.mkdir(parents=True, exist_ok=True)
    report = TRIAD_DIR / "codex-review.md"
    instructions = (PROMPTS / "codex-reviewer.md").read_text(encoding="utf-8")
    prompt = f"Task being reviewed:\n{args.task}\n\n{instructions}"
    cmd = [
        str(CODEX), "exec", "-C", str(ROOT), "-s", "read-only", "--ephemeral",
        "--ignore-user-config", "-m", CODEX_REVIEW_MODEL,
        "-c", f'model_reasoning_effort="{normalize_codex_effort(CODEX_EFFORT)}"', "-o", str(report), "-",
    ]
    result = run(cmd, input_text=prompt)
    if result.returncode == 0:
        print(f"Codex review: {report}")
    return result.returncode


def claude_audit(args: argparse.Namespace) -> int:
    TRIAD_DIR.mkdir(parents=True, exist_ok=True)
    report = TRIAD_DIR / "claude-audit.md"
    instructions = (PROMPTS / "claude-auditor.md").read_text(encoding="utf-8")
    prompt = f"Task being audited:\n{args.task}\n\n{instructions}"
    cmd = [
        str(CLAUDE), "-p", "--model", os.environ.get("TAU_CLAUDE_AUDIT_MODEL", "sonnet"),
        "--effort", "low", "--restricted", "--permission-mode", "plan",
        "--permission-prompts", "none", "--allowedTools", "Read", "Glob", "Grep",
        "--no-session-persistence", prompt,
    ]
    result = run(cmd, capture=True)
    report.write_text(result.stdout, encoding="utf-8")
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    print(result.stdout)
    if result.returncode == 0:
        print(f"Claude audit: {report}")
    return result.returncode


def review_is_clean() -> bool:
    try:
        first = next(line.strip() for line in (TRIAD_DIR / "codex-review.md").read_text(encoding="utf-8").splitlines() if line.strip())
    except (OSError, StopIteration):
        return False
    return first == "VERDICT: CLEAN"


def full_run(args: argparse.Namespace) -> int:
    # Fail before local edits if the independent final auditor is unavailable.
    if not claude_logged_in():
        print("STOP: sign in to Claude before starting the full triad; no project files changed.")
        return 2
    if not confirm_codex_configuration("run"):
        return 4
    steps = [codex_plan, qwen_patch, gate, codex_review]
    if "figma.com/" in args.task:
        steps.insert(0, design_brief)
    for step in steps:
        code = step(args)
        if code:
            print(f"STOP: {step.__name__} returned {code}")
            return code
    if not review_is_clean():
        plan_code = codex_plan(args, repair_report=TRIAD_DIR / "codex-review.md")
        if plan_code:
            return plan_code
        repair_plan = json.loads((TRIAD_DIR / "codex-repair-plan.json").read_text(encoding="utf-8"))
        if repair_plan["status"] == "no_change":
            print("STOP: Codex found no valid in-scope repair; human review is required.")
            return 1
        patch_code = qwen_patch(args, repair=True)
        if patch_code:
            return patch_code
        gate_code = gate(args)
        if gate_code:
            return gate_code
        if codex_review(args) or not review_is_clean():
            print("STOP: Codex review still has findings after its single repair pass.")
            return 1
    audit_code = claude_audit(args)
    if audit_code:
        return audit_code
    print("STOP: triad complete. Human approval is required before any Quartus run.")
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    sub = result.add_subparsers(dest="command", required=True)
    sub.add_parser("doctor").set_defaults(func=doctor)
    sub.add_parser("gate").set_defaults(func=gate)
    for name, func in (
        ("figma-brief", design_brief), ("plan", codex_plan),
        ("develop", qwen_patch), ("review", codex_review),
        ("audit", claude_audit), ("run", full_run),
    ):
        command = sub.add_parser(name)
        command.add_argument("task")
        command.set_defaults(func=func)
    return result


def main() -> int:
    args = parser().parse_args()
    if args.command in {"figma-brief", "plan", "review"}:
        if not confirm_codex_configuration(args.command):
            return 4
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
