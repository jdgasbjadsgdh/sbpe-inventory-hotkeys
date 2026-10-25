"""Build the installer from an unedited, SHA-pinned WebBreak source file."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

root=Path(__file__).resolve().parent
if len(sys.argv)!=2:
    raise SystemExit('Usage: python3 BUILD.py /path/to/original/VR1574197275.mvmmoclient.js (requires Node.js)')
original=Path(sys.argv[1]).read_text(encoding='utf-8')
signatures=json.loads((root/'inventory-native-signatures.json').read_text())
if hashlib.sha256(original.encode()).hexdigest()!=signatures['upstream_sha256']:
    raise SystemExit('Use the unedited WebBreak source at pinned commit ec7fe74579ec6ad7a5e663a2a55672de62fc8361. No output written.')
payload={'upstream':original,'plugin':(root/'inventory-hotkeys.js').read_text(),'signatures':signatures}
script="const fs=require('fs'),p=JSON.parse(fs.readFileSync(0,'utf8'));process.stdout.write(require(process.argv[1]).update(p.upstream,p));"
result=subprocess.run(['node','-e',script,str(root/'inventory-install-core.js')],input=json.dumps(payload),text=True,capture_output=True,check=True)
(root/'payload-data.js').write_text('window.SBPEInventoryPayload='+json.dumps(payload)+';\n')
out=root/'www.starbreak.com';out.mkdir(exist_ok=True)
(out/'VR1574197275.mvmmoclient.js').write_text(result.stdout)
print('Built INSTALL.html payload and combined client. No browser was changed or reloaded.')
