#!/usr/bin/env python3
"""Safety checks for plans and model-generated patch path validation."""

from io import StringIO
from unittest import mock
import unittest

import director


class PlanValidationTests(unittest.TestCase):
    def test_accepts_scoped_patch_plan(self):
        director.validate_plan({
            "status": "patch",
            "task_packet": "Edit the module and run its targeted test.",
            "allowed_files": ["src/fpga/core/example.sv"],
        })

    def test_accepts_no_change_plan(self):
        director.validate_plan({
            "status": "no_change",
            "task_packet": "No code change is required.",
            "allowed_files": [],
        })

    def test_rejects_path_traversal(self):
        with self.assertRaises(ValueError):
            director.validate_plan({
                "status": "patch", "task_packet": "Edit a module.",
                "allowed_files": ["../../outside.txt"],
            })

    def test_rejects_windows_absolute_path(self):
        with self.assertRaises(ValueError):
            director.validate_plan({
                "status": "patch", "task_packet": "Edit a module.",
                "allowed_files": [r"C:\outside.txt"],
            })


class PatchPathTests(unittest.TestCase):
    def test_accepts_only_allowlisted_paths(self):
        diff = "diff --git a/src/fpga/core/example.sv b/src/fpga/core/example.sv\n"
        self.assertEqual(
            director.validate_patch_paths(diff, ["src/fpga/core/example.sv"]),
            {"src/fpga/core/example.sv"},
        )

    def test_rejects_unlisted_path(self):
        diff = "diff --git a/src/fpga/core/other.sv b/src/fpga/core/other.sv\n"
        with self.assertRaises(ValueError):
            director.validate_patch_paths(diff, ["src/fpga/core/example.sv"])

    def test_rejects_parent_path(self):
        diff = "diff --git a/../outside.txt b/../outside.txt\n"
        with self.assertRaises(ValueError):
            director.validate_patch_paths(diff, ["../outside.txt"])

    def test_rejects_non_diff_output(self):
        with self.assertRaises(ValueError):
            director.validate_patch_paths("Here is the patch:\n", ["example.sv"])

    def test_rejects_symlink_patch(self):
        diff = (
            "diff --git a/src/fpga/core/example.sv b/src/fpga/core/example.sv\n"
            "new file mode 120000\n"
        )
        with self.assertRaises(ValueError):
            director.validate_patch_paths(diff, ["src/fpga/core/example.sv"])

    def test_rejects_symlink_mode_change(self):
        diff = (
            "diff --git a/src/fpga/core/example.sv b/src/fpga/core/example.sv\n"
            "new mode 120000\n"
        )
        with self.assertRaises(ValueError):
            director.validate_patch_paths(diff, ["src/fpga/core/example.sv"])

    def test_recognizes_declined_patch_without_applying_it(self):
        self.assertTrue(director.qwen_declined_patch("\nNO_CHANGE\n"))
        self.assertFalse(director.qwen_declined_patch("NO_CHANGE plus a diff"))


class CodexApprovalPolicyTests(unittest.TestCase):
    def test_luna_and_terra_are_preferred_without_approval(self):
        self.assertEqual(
            director.codex_approval_reasons("plan", model="gpt-5.6-luna", effort="low"),
            [],
        )
        self.assertEqual(
            director.codex_approval_reasons("review", review_model="gpt-5.6-terra", effort="medium"),
            [],
        )

    def test_astra_requires_approval_for_relevant_command(self):
        self.assertTrue(
            director.codex_approval_reasons("plan", model="gpt-6-astra", effort="low")
        )
        self.assertTrue(
            director.codex_approval_reasons(
                "run", model="gpt-5.6-luna", review_model="gpt-6-astra", effort="low"
            )
        )
        self.assertEqual(director.codex_approval_reasons("gate", effort="xhigh"), [])

    def test_nonpreferred_model_requires_approval(self):
        self.assertTrue(
            director.codex_approval_reasons("plan", model="gpt-5.6-sol", effort="low")
        )

    def test_xhigh_ultra_and_max_efforts_require_approval(self):
        for effort in ("xhigh", "extra high", "ultra", "max"):
            with self.subTest(effort=effort):
                self.assertTrue(
                    director.codex_approval_reasons(
                        "plan", model="gpt-5.6-luna", effort=effort
                    )
                )
        self.assertEqual(director.normalize_codex_effort("extra high"), "xhigh")

    def test_approval_prompt_requires_interactive_terminal(self):
        class NonInteractiveInput(StringIO):
            def isatty(self):
                return False

        with mock.patch.object(director.sys, "stdin", NonInteractiveInput("")):
            self.assertFalse(
                director.confirm_codex_configuration(
                    "plan", model="gpt-6-astra", effort="low"
                )
            )

    def test_user_can_approve_or_decline_expensive_configuration(self):
        class InteractiveInput(StringIO):
            def isatty(self):
                return True

        with mock.patch.object(director.sys, "stdin", InteractiveInput("")):
            with mock.patch("builtins.input", return_value="yes"):
                self.assertTrue(
                    director.confirm_codex_configuration(
                        "plan", model="gpt-6-astra", effort="low"
                    )
                )
            with mock.patch("builtins.input", return_value="no"):
                self.assertFalse(
                    director.confirm_codex_configuration(
                        "plan", model="gpt-6-astra", effort="low"
                    )
                )


if __name__ == "__main__":
    unittest.main()
