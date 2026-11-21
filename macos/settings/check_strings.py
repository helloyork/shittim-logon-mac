"""Every string the Mac settings app looks up has a row.

The Mac counterpart of tools/i18n/check.py for the rows the app uses: each T("...") in
the Swift files and each L"..." / tr:@"..." in bridge.mm and bridge_files.h
(a line marked `// i18n: no` is skipped) must be the key of a row in
ui/strings.h or macos/settings/strings_mac.h, or it shows in Chinese on every language.

    python macos/settings/check_strings.py        (from the repository root)
"""
import glob
import re
import sys

ROWS = set()
for path in ('ui/strings.h', 'macos/settings/strings_mac.h'):
    text = open(path, encoding='utf-8').read()
    ROWS |= set(re.findall(r'\{\s*L"((?:[^"\\]|\\.)*)"', text))

used = set()
for path in glob.glob('macos/settings/*.swift'):
    text = open(path, encoding='utf-8').read()
    for m in re.findall(r'\bT\("((?:[^"\\]|\\.)*)"', text):
        used.add((m, path))
for path in ('macos/settings/bridge.mm', 'macos/settings/bridge_files.h'):
    name = path.split('/')[-1]
    for line in open(path, encoding='utf-8'):
        if 'i18n: no' in line:          # a literal deliberately not looked up
            continue
        for m in re.findall(r'L"((?:[^"\\]|\\.)*)"', line):
            if m.replace('\\r\\n', '').strip(' ·|%sd.1f'):   # separators and bare conversions are not rows
                used.add((m, name))
        for m in re.findall(r'tr:@"((?:[^"\\]|\\.)*)"', line):
            used.add((m, name))

missing = sorted((m, f) for m, f in used if m not in ROWS)
print(f'{len(used)} strings used, {len(missing)} without a row')
for m, f in missing:
    print(f'  {f}: {m}')
sys.exit(1 if missing else 0)
