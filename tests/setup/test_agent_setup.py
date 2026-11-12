"""Setup tests use temporary roots only; never read the operator's configuration."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import tomllib
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / "scripts/setup-esp32-agent.py"
SPEC = importlib.util.spec_from_file_location("ngk_agent_setup", SCRIPT)
setup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(setup)


class AgentSetupTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "project"
        self.home = Path(self.temporary.name) / "codex-home"
        self.root.mkdir()
        self.home.mkdir()
        self.user = self.home / "config.toml"
        self.project = self.root / ".codex/config.toml"

    def apply(self, action="apply"):
        return setup.configure(self.root, self.home, action)

    def test_check_is_read_only_and_apply_is_byte_idempotent(self):
        self.assertTrue(self.apply("check"))
        self.assertFalse(self.project.exists())
        self.assertFalse(self.user.exists())
        self.assertFalse((self.root / "local").exists())
        self.assertTrue(self.apply())
        original = self.project.read_bytes(), self.user.read_bytes()
        self.assertFalse(self.apply())
        self.assertFalse(self.apply("check"))
        self.assertEqual(original, (self.project.read_bytes(), self.user.read_bytes()))
        config = tomllib.loads(self.project.read_text())
        tools = config["mcp_servers"]["espressif-engineering"]["tools"]
        self.assertEqual(set(tools), {"ts_skill_list", "ts_skill_get"})
        self.assertTrue(all(item == {"approval_mode": "approve"} for item in tools.values()))

    def test_reuses_aliased_global_urls_and_preserves_preferences(self):
        text = 'model = "existing-model"\napproval_policy = "never"\n'
        for name, url in zip(("mydocs", "myengineering", "mylvgl"), setup.SERVERS.values()):
            text += f'\n[mcp_servers.{name}]\nurl = {json.dumps(url.upper() if name == "mydocs" else url)}\n'
        # URL paths are case-sensitive; normalize only scheme/host/default port/slash.
        text = text.replace("/DOCS", "/docs/").replace("MCP.ESPRESSIF.COM/", "MCP.ESPRESSIF.COM:443/")
        self.user.write_text(text)
        self.apply()
        self.assertTrue(self.user.read_text().startswith(text))
        config = tomllib.loads(self.project.read_text())["mcp_servers"]
        self.assertEqual(set(config), {"myengineering"})
        self.assertNotIn("url", config["myengineering"])
        self.assertEqual(set(config["myengineering"]["tools"]), set(setup.READ_TOOLS))
        self.assertFalse(self.apply())

    def test_private_content_addressed_backups_and_undo_preserve_neighbors(self):
        original_user = b'model = "keep"'
        original_project = b'[features]\nexisting = true\n'
        self.user.write_bytes(original_user)
        self.project.parent.mkdir()
        self.project.write_bytes(original_project)
        self.apply()
        backups = self.root / "local/esp32-setup/backups"
        for kind, content in (("user", original_user), ("project", original_project)):
            backup = backups / f"{kind}-config-{hashlib.sha256(content).hexdigest()}.toml"
            self.assertEqual(backup.read_bytes(), content)
            self.assertEqual(stat.S_IMODE(backup.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(backups.stat().st_mode), 0o700)
        with self.project.open("a") as stream:
            stream.write('\n[features.later]\nenabled = true\n')
        with self.user.open("a") as stream:
            stream.write('\n[preferences]\nkeep = "later"\n')
        self.apply("undo")
        self.assertEqual(self.user.read_bytes(), original_user + b'\n[preferences]\nkeep = "later"\n')
        self.assertEqual(self.project.read_bytes(), original_project + b'\n[features.later]\nenabled = true\n')
        self.assertFalse(self.apply("undo"))

    def test_refuses_conflict_without_exposing_credentials_or_writing(self):
        secret = "do-not-print-this"
        text = f'[mcp_servers.espressif-docs]\nurl = "https://user:{secret}@example.invalid/mcp"\n'
        self.user.write_text(text)
        with self.assertRaises(setup.SetupError) as context:
            self.apply()
        self.assertNotIn(secret, str(context.exception))
        self.assertEqual(self.user.read_text(), text)
        self.assertFalse(self.project.exists())
        self.assertFalse((self.root / "local").exists())
        self.assertIsNone(setup.normalize_url("file:///private/config"))
        self.assertIsNone(setup.normalize_url("https://user:secret@mcp.espressif.com/docs"))

    def test_explicit_untrusted_and_tool_denial_are_preserved(self):
        text = f'[projects.{json.dumps(str(self.root))}]\ntrust_level = "untrusted"\n'
        self.user.write_text(text)
        with self.assertRaises(setup.SetupError):
            self.apply()
        self.assertEqual(self.user.read_text(), text)
        self.user.write_text('[mcp_servers.engineering]\nurl = "https://ts-mcp.espressif.com/mcp"\n'
                             '[mcp_servers.engineering.tools.ts_skill_get]\napproval_mode = "deny"\n')
        with self.assertRaises(setup.SetupError):
            self.apply()
        self.assertFalse(self.project.exists())

    def test_refuses_global_alias_overridden_by_another_project_url(self):
        self.user.write_text('[mcp_servers.custom]\nurl = "https://ts-mcp.espressif.com/mcp"\n')
        self.project.parent.mkdir()
        text = '[mcp_servers.custom]\nurl = "https://other.invalid/mcp"\n'
        self.project.write_text(text)
        with self.assertRaises(setup.SetupError):
            self.apply()
        self.assertEqual(self.project.read_text(), text)
        self.assertFalse((self.root / "local").exists())

    def test_refuses_project_directory_symlink_to_user_configuration(self):
        text = 'model = "preserve"\n'
        self.user.write_text(text)
        self.project.parent.symlink_to(self.home, target_is_directory=True)
        with self.assertRaises(setup.SetupError):
            self.apply()
        self.assertEqual(self.user.read_text(), text)

    def test_refuses_symlinked_backup_parent_before_configuration_write(self):
        self.user.write_text('model = "preserve"\n')
        (self.root / "local").symlink_to(self.home, target_is_directory=True)
        with self.assertRaises(setup.SetupError):
            self.apply()
        self.assertFalse(self.project.exists())

    def test_legacy_project_block_is_preserved_on_apply_and_removed_on_undo(self):
        self.project.parent.mkdir()
        entries = {name: {"url": url, "startup_timeout_sec": 30, "tool_timeout_sec": 90}
                   for name, url in setup.SERVERS.items()}
        entries["espressif-engineering"]["tools"] = {tool: {"approval_mode": "approve"} for tool in setup.READ_TOOLS}
        payload = setup.render_servers(entries)
        for tool, comment in zip(setup.READ_TOOLS, setup.LEGACY_COMMENTS):
            payload = payload.replace(f"[mcp_servers.espressif-engineering.tools.{tool}]", comment + f"[mcp_servers.espressif-engineering.tools.{tool}]")
        original = setup.MCP_BEGIN + "\n\n" + payload + "\n" + setup.MCP_END + "\n"
        self.project.write_text(original)
        self.apply()
        self.assertEqual(self.project.read_text(), original)
        self.apply("undo")
        self.assertEqual(self.project.read_text(), "")

    def test_undo_refuses_edited_owned_block(self):
        self.apply()
        changed = self.project.read_text().replace("tool_timeout_sec = 90", "tool_timeout_sec = 91", 1)
        self.project.write_text(changed)
        user = self.user.read_bytes()
        with self.assertRaises(setup.SetupError):
            self.apply("undo")
        self.assertEqual(self.project.read_text(), changed)
        self.assertEqual(self.user.read_bytes(), user)

    def test_legacy_trust_is_normalized_and_known_project_block_is_undoable(self):
        payload = f'[projects.{json.dumps(str(self.root))}]\ntrust_level = "trusted"\n'
        self.user.write_text('model = "keep"\n' + setup.TRUST_BEGIN + "\n" + payload)
        self.apply()
        self.assertIn(setup.TRUST_END, self.user.read_text())
        self.assertFalse(self.apply())
        self.apply("undo")
        self.assertEqual(self.user.read_text(), 'model = "keep"\n')

    def test_preserves_existing_crlf_bytes_through_apply_and_undo(self):
        original = b'model = "keep"\r\n'
        self.user.write_bytes(original)
        self.apply()
        self.assertTrue(self.user.read_bytes().startswith(original))
        self.apply("undo")
        self.assertEqual(self.user.read_bytes(), original)

    def test_cli_requires_action_and_honors_codex_home_in_temporary_root(self):
        copied = self.root / "scripts/setup-esp32-agent.py"
        copied.parent.mkdir()
        copied.write_bytes(SCRIPT.read_bytes())
        environment = dict(os.environ, CODEX_HOME=str(self.home))
        command = [sys.executable, str(copied)]
        self.assertEqual(subprocess.run(command, env=environment, capture_output=True).returncode, 2)
        self.assertFalse(self.user.exists())
        self.assertEqual(subprocess.run(command + ["--check"], env=environment, capture_output=True).returncode, 1)
        self.assertFalse(self.project.exists())
        self.assertEqual(subprocess.run(command + ["--apply"], env=environment, capture_output=True).returncode, 0)
        self.assertIn(str(self.root), self.user.read_text())
        self.assertFalse(self.apply("check"))


if __name__ == "__main__":
    unittest.main()
