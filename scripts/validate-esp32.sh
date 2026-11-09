#!/bin/sh
# Static pin audit only. Never opens a device or builds/uploads firmware.
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd -- "$repo_root"

if ! sha256sum --check --status tools/esp32-audit-upstream.sha256; then
    printf '%s\n' 'ESP32 audit: installed upstream files differ from the audited snapshot.' >&2
    exit 2
fi
if ! sha256sum --check --status tools/esp32-audit-board.sha256; then
    printf '%s\n' 'ESP32 audit: board source or input changed; review and refresh the audit snapshot.' >&2
    exit 2
fi

PYTHONDONTWRITEBYTECODE=1 exec "$repo_root/.venv/bin/python" \
    "$repo_root/.agents/skills/esp32/scripts/validate_pinmap.py" \
    --format json "$repo_root/tools/esp32-audit-board.json"
