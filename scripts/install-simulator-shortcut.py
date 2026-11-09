#!/usr/bin/env python3
"""Create a per-user desktop entry for this checkout; no system installation."""
from pathlib import Path
import os

root = Path(__file__).resolve().parent.parent
applications = Path(os.environ.get('XDG_DATA_HOME', str(Path.home() / '.local/share'))) / 'applications'
entry = applications / 'ngk-ecu-simulator.desktop'
launcher = str(root / 'scripts/simulator').replace('%', '%%')
for before, after in [('\\', '\\\\'), ('"', '\\"'), ('`', '\\`'), ('$', '\\$')]:
    launcher = launcher.replace(before, after)
text = f'''[Desktop Entry]
Type=Application
Name=NGK ECU Simulador
Comment=Simulação local de condução e falhas para o painel NGK ECU
Exec="{launcher}"
Icon={root / 'assets/simulator.svg'}
Terminal=false
Categories=Development;Utility;
StartupWMClass=ngk-ecu-simulator
X-NGK-ECU-Simulator=true
'''
if entry.exists() and 'X-NGK-ECU-Simulator=true' not in entry.read_text():
    raise SystemExit('Um atalho com esse nome já existe; ele foi preservado.')
applications.mkdir(parents=True, exist_ok=True)
entry.write_text(text)
entry.chmod(0o755)
print('Atalho NGK ECU Simulador instalado no menu de aplicativos do usuário.')
