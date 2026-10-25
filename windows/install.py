"""Copy only this plugin and its helpers; back up replacements, preserve keys."""
import ast
import datetime
from pathlib import Path
import shutil
import sys

SOURCE = Path(__file__).resolve().parent


def install(target):
    target = Path(target).expanduser().resolve()
    if target == SOURCE.resolve() or not (target/'manager.py').is_file() or not (target/'plugins').is_dir() or not (target/'pypy').is_dir():
        raise ValueError('Choose your SBPE folder containing manager.py, plugins and pypy')
    plugin = 'plugins/inventory_hotkeys.py'
    text = (SOURCE/plugin).read_text(encoding='utf-8')
    old = target/plugin
    if old.exists():
        tree = ast.parse(old.read_text(encoding='utf-8'))
        for node in tree.body:
            if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'SLOT_KEYS' for t in node.targets):
                keys = ast.literal_eval(node.value)
                if not isinstance(keys, dict) or any(not isinstance(k, int) or not 1 <= k <= 12 or not isinstance(v, str) for k,v in keys.items()):
                    raise ValueError('Existing SLOT_KEYS block is invalid; no files changed')
                start = text.index('SLOT_KEYS = ')
                end = text.index('\n\nimport logging', start)
                text = text[:start]+'SLOT_KEYS = '+repr(keys)+text[end:]
                break
    files = [(SOURCE/name, target/name) for name in ('inventory_hotkeys_native.py', 'inventory_hotkeys_resolver.py', 'inventory_hotkeys_x86.py')]
    files += [(p, target/p.relative_to(SOURCE)) for p in (SOURCE/'inventory_hotkeys_vendor').rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    backup = target/'inventory-hotkeys-backups'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    files += [(SOURCE/plugin, target/plugin)]
    for source,dest in files:
        if dest.exists():
            saved = backup/dest.relative_to(target);saved.parent.mkdir(parents=True, exist_ok=True);shutil.copy2(str(dest), str(saved))
    for source,dest in files:
        dest.parent.mkdir(parents=True, exist_ok=True)
        temp = dest.with_name(dest.name+'.inventory-new')
        if dest == target/plugin:
            temp.write_text(text, encoding='utf-8')
        else:
            shutil.copy2(str(source), str(temp))
        temp.replace(dest)
    print('Installed. Edit plugins/inventory_hotkeys.py to choose SLOT_KEYS. Existing keys preserved.')
    print('No game was started or restarted.')


if __name__ == '__main__':
    try:
        install(sys.argv[1])
    except (IndexError, ValueError, OSError) as error:
        print('Install failed: '+str(error));sys.exit(1)
