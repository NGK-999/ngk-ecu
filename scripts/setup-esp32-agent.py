#!/usr/bin/env python3
"""Configure only project MCP entries and the necessary Codex project trust."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import tomllib
from urllib.parse import urlsplit

SERVERS = {
    "espressif-docs": "https://mcp.espressif.com/docs",
    "espressif-engineering": "https://ts-mcp.espressif.com/mcp",
    "lvgl-docs": "https://lvgl.mcp.kapa.ai/",
}
READ_TOOLS = ("ts_skill_list", "ts_skill_get")
MCP_BEGIN, MCP_END = "# BEGIN NGK ESP32 MCP SETUP", "# END NGK ESP32 MCP SETUP"
TRUST_BEGIN = "# NGK ESP32 setup: enable project-scoped instructions and MCP configuration."
TRUST_END = "# END NGK ESP32 PROJECT TRUST SETUP"
STAMP = re.compile(r"# NGK content-sha256: ([0-9a-f]{64}); added-newline=([01])\n")
LEGACY_COMMENTS = (
    "# Read-only discovery explicitly requested for this setup.\n",
    "# Read-only skill retrieval explicitly requested for this setup.\n",
)


class SetupError(Exception):
    """A bounded error message that never contains configuration contents."""


def normalize_url(value):
    if not isinstance(value, str) or any(char.isspace() for char in value):
        return None
    try:
        uri = urlsplit(value)
        if uri.scheme.lower() != "https" or not uri.hostname or uri.username or uri.password or uri.query or uri.fragment:
            return None
        return ("https", uri.hostname.lower(), 443 if uri.port is None else uri.port, uri.path.rstrip("/"))
    except ValueError:
        return None


def parse(text):
    try:
        return tomllib.loads(text)
    except tomllib.TOMLDecodeError:
        raise SetupError("Configuração TOML inválida; nenhum conteúdo foi exposto.") from None


def read(path):
    if path.is_symlink():
        raise SetupError("Arquivo de configuração é um link simbólico; alteração recusada.")
    try:
        return path.read_bytes().decode("utf-8") if path.exists() else ""
    except UnicodeError:
        raise SetupError("Configuração não é UTF-8; alteração recusada.") from None


def key(value):
    return value if re.fullmatch(r"[A-Za-z0-9_-]+", value) else json.dumps(value, ensure_ascii=False)


def render_servers(servers):
    sections = []
    for name, server in servers.items():
        if "url" in server:
            sections.append(f"[mcp_servers.{key(name)}]\nurl = {json.dumps(server['url'])}\n"
                            "startup_timeout_sec = 30\ntool_timeout_sec = 90")
    for name, server in servers.items():
        for tool in server.get("tools", {}):
            sections.append(f'[mcp_servers.{key(name)}.tools.{key(tool)}]\napproval_mode = "approve"')
    return "\n\n".join(sections)


def managed(text, begin, end, trust_payload=None):
    starts = list(re.finditer(rf"(?m)^{re.escape(begin)}$", text))
    ends = list(re.finditer(rf"(?m)^{re.escape(end)}(?:\n|$)", text))
    if not starts and not ends:
        return None
    if len(starts) != 1 or len(ends) > 1 or (ends and ends[0].start() < starts[0].end()):
        raise SetupError("Marcadores de setup ambíguos; alteração recusada.")
    start = starts[0].start()
    finish = ends[0].end() if ends else len(text)
    body = text[starts[0].end():ends[0].start() if ends else finish].lstrip("\n")
    stamp = STAMP.match(body)
    if stamp:
        payload = body[stamp.end():].removesuffix("\n")
        if hashlib.sha256(payload.encode()).hexdigest() != stamp[1]:
            raise SetupError("Bloco de setup foi editado pelo usuário; alteração recusada.")
        if trust_payload is not None and payload != trust_payload:
            raise SetupError("Bloco de confiança pertence a outro caminho; configuração preservada.")
        if stamp[2] == "1":
            if start == 0 or text[start - 1] != "\n":
                raise SetupError("Separador do bloco foi editado; alteração recusada.")
            start -= 1
    else:
        payload = body.strip()
        if trust_payload is not None:
            recognized = payload == trust_payload
        else:
            data = parse(payload)
            entries = data.get("mcp_servers", {})
            recognized = set(data) == {"mcp_servers"}
            for name, server in entries.items():
                recognized &= set(server) <= {"url", "startup_timeout_sec", "tool_timeout_sec", "tools"}
                if "url" in server:
                    recognized &= (name in SERVERS and server["url"] == SERVERS.get(name)
                                   and server.get("startup_timeout_sec") == 30 and server.get("tool_timeout_sec") == 90)
                for tool, policy in server.get("tools", {}).items():
                    recognized &= tool in READ_TOOLS and policy == {"approval_mode": "approve"}
            clean = payload
            for comment in LEGACY_COMMENTS:
                clean = clean.replace(comment, "")
            recognized &= clean == render_servers(entries)
        if not recognized or (not ends and trust_payload is None):
            raise SetupError("Bloco legado não reconhecido ou modificado; alteração recusada.")
    return start, finish, payload, not ends


def block(text, begin, end, payload, span=None):
    prefix, suffix = (text[:span[0]], text[span[1]:]) if span else (text, "")
    separator = bool(prefix and not prefix.endswith("\n"))
    digest = hashlib.sha256(payload.encode()).hexdigest()
    return (prefix + ("\n" if separator else "") + begin + "\n"
            + f"# NGK content-sha256: {digest}; added-newline={int(separator)}\n"
            + payload + "\n" + end + "\n" + suffix)


def missing_servers(global_config, project_config):
    scopes = [project_config.get("mcp_servers", {}), global_config.get("mcp_servers", {})]
    if any(not isinstance(scope, dict) or any(not isinstance(entry, dict) for entry in scope.values()) for scope in scopes):
        raise SetupError("Formato das tabelas MCP inválido; configuração preservada.")
    additions = {}
    for name, url in SERVERS.items():
        target = normalize_url(url)
        if any(name in scope and ("command" in scope[name] or
               ("url" in scope[name] and normalize_url(scope[name]["url"]) != target)) for scope in scopes):
            raise SetupError(f"Conflito de URL no serviço {name}; configuração preservada.")
        matches = [(alias, entry) for scope in scopes for alias, entry in scope.items()
                   if normalize_url(entry.get("url")) == target]
        if any(entry.get("enabled") is False for _, entry in matches):
            raise SetupError(f"Serviço {name} foi desativado explicitamente; configuração preservada.")
        alias = matches[0][0] if matches else name
        if any("url" in scope.get(alias, {}) and normalize_url(scope[alias]["url"]) != target for scope in scopes):
            raise SetupError("Alias MCP tem URLs divergentes entre usuário e projeto; configuração preservada.")
        if any("command" in scope.get(alias, {}) for scope in scopes):
            raise SetupError("Transporte MCP conflitante; configuração preservada.")
        if not matches:
            additions[alias] = {"url": url, "startup_timeout_sec": 30, "tool_timeout_sec": 90}
        if name == "espressif-engineering":
            for tool in READ_TOOLS:
                policies = [scope.get(alias, {}).get("tools", {}).get(tool, {}).get("approval_mode") for scope in scopes]
                if any(policy not in (None, "approve") for policy in policies):
                    raise SetupError("Permissão explícita de leitura conflitante; configuração preservada.")
                if "approve" not in policies:
                    additions.setdefault(alias, {}).setdefault("tools", {})[tool] = {"approval_mode": "approve"}
    return additions


def backup(root, path, kind):
    if not path.exists():
        return
    directory = root / "local/esp32-setup/backups"
    if (root / "local").is_symlink():
        raise SetupError("Raiz de backup é link simbólico; alteração recusada.")
    for parent in (directory.parent, directory):
        if parent.is_symlink():
            raise SetupError("Diretório de backup é link simbólico; alteração recusada.")
        parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        parent.chmod(0o700)
    content = path.read_bytes()
    target = directory / f"{kind}-config-{hashlib.sha256(content).hexdigest()}.toml"
    if target.is_symlink():
        raise SetupError("Backup é link simbólico; alteração recusada.")
    try:
        descriptor = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        if target.read_bytes() != content:
            raise SetupError("Backup existente diverge do conteúdo esperado.") from None
        target.chmod(0o600)
    else:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)


def configure(root, codex_home, action):
    if action not in {"check", "apply", "undo"}:
        raise ValueError("Ação inválida")
    root, codex_home = Path(root).resolve(), Path(codex_home).resolve()
    project, user = root / ".codex/config.toml", codex_home / "config.toml"
    if project.parent.is_symlink() or project.resolve() == user.resolve():
        raise SetupError("Configurações de projeto/usuário compartilham alvo ou usam link; alteração recusada.")
    project_text, user_text = read(project), read(user)
    project_config, user_config = parse(project_text), parse(user_text)
    trust_payload = f'[projects.{json.dumps(str(root), ensure_ascii=False)}]\ntrust_level = "trusted"'
    project_span = managed(project_text, MCP_BEGIN, MCP_END)
    user_span = managed(user_text, TRUST_BEGIN, TRUST_END, trust_payload)
    if action == "undo":
        next_project = project_text[:project_span[0]] + project_text[project_span[1]:] if project_span else project_text
        next_user = user_text[:user_span[0]] + user_text[user_span[1]:] if user_span else user_text
    else:
        projects = user_config.get("projects", {})
        if not isinstance(projects, dict) or not isinstance(projects.get(str(root), {}), dict):
            raise SetupError("Formato da confiança de projeto inválido; configuração preservada.")
        trust = projects.get(str(root), {}).get("trust_level")
        if trust not in (None, "trusted"):
            raise SetupError("Projeto tem confiança explícita diferente de trusted; alteração recusada.")
        additions = missing_servers(user_config, project_config)
        next_project = project_text
        if additions:
            payload = render_servers(additions)
            if project_span:
                payload = project_span[2] + "\n\n" + payload
            next_project = block(project_text, MCP_BEGIN, MCP_END, payload, project_span)
        next_user = user_text
        if trust is None or (user_span and user_span[3]):
            next_user = block(user_text, TRUST_BEGIN, TRUST_END, trust_payload, user_span)
    updates = [(path, text, kind) for path, text, previous, kind in (
        (project, next_project, project_text, "project"), (user, next_user, user_text, "user")) if text != previous]
    for _, text, _ in updates:
        parse(text)
    if action != "check":
        for path, _, kind in updates:
            backup(root, path, kind)
        for path, text, _ in updates:
            path.parent.mkdir(parents=True, exist_ok=True)
            descriptor, temporary = tempfile.mkstemp(prefix=".ngk-setup-", dir=path.parent)
            try:
                with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as stream:
                    stream.write(text)
                os.replace(temporary, path)
            finally:
                Path(temporary).unlink(missing_ok=True)
    return bool(updates)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_mutually_exclusive_group(required=True)
    for action in ("check", "apply", "undo"):
        actions.add_argument("--" + action, action="store_const", const=action, dest="action")
    args = parser.parse_args()
    if sys.version_info[:2] != (3, 12):
        parser.error("Use Python 3.12 da .venv local.")
    root = Path(__file__).resolve().parents[1]
    codex_home = Path(os.environ.get("CODEX_HOME", str(Path.home() / ".codex")))
    try:
        changed = configure(root, codex_home, args.action)
    except SetupError as error:
        print(f"Setup recusado: {error}")
        return 2
    except OSError:
        # OSError paths may contain personal information; do not echo them.
        print("Falha de E/S: alterações podem ser parciais; confira os backups privados antes de repetir.")
        return 2
    print("Setup requer alterações." if args.action == "check" and changed else "Setup conferido.")
    return int(args.action == "check" and changed)


if __name__ == "__main__":
    raise SystemExit(main())
