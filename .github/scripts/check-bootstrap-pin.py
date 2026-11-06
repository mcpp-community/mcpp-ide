"""Check the project's shim pin against the declared CI bootstrap before installation."""
import json
from pathlib import Path

values = dict(line.split('=', 1) for line in Path('.github/versions.env').read_text().splitlines()
              if line and not line.startswith('#'))
declared = values['MCPP_VERSION']
project = json.loads(Path('.xlings.json').read_text())['workspace']['mcpp']
if project != declared:
    raise SystemExit(f'bootstrap mismatch: .xlings.json selects {project}, versions.env installs {declared}')
print(f'project shim and CI bootstrap both select mcpp {declared}')
