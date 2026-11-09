#!/bin/sh
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"
export PYTHONPATH="$repo_root/simulator${PYTHONPATH:+:$PYTHONPATH}"
export QT_QPA_PLATFORM=offscreen
exec .venv/bin/python -m unittest discover -s tests/simulator -v
