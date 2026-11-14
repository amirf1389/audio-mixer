#!/usr/bin/env python3
# Finds interface text that never reaches the translator: string literals given to text / label / title / subtitle / note / placeholderText /
# tip properties, and the t / s / d / note fields of model arrays, that are not inside i18n.t(...). Prints file:line and the string.
# --keys: also lists every i18n.t("...") key in the QML that is missing from i18n/ui-keys.json.
import json, os, re, sys
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'native', 'amixos'); qml = os.path.join(root, 'qml')
PROP = re.compile(r'\b(text|label|title|subtitle|note|placeholderText|tip|hint|caption|help)\s*:\s*"((?:[^"\\]|\\.)*)"')
FIELD = re.compile(r'\b(t|s|d|note|desc|title|label|hint|help)\s*:\s*"((?:[^"\\]|\\.)*)"')
KEY = re.compile(r'i18n\.t\(\s*"((?:[^"\\]|\\.)*)"')
def visible(s): return re.search(r'[A-Za-z]{2,}', s) and not re.fullmatch(r'[A-Z0-9_.\-/ ]{1,3}|[a-z]+[A-Z]\w*|#[0-9a-fA-F]+|qrc:.*|https?:.*|[\w./-]+\.(qml|png|svg|json)', s)
unwrapped, keys = [], set()
for dp, _, fs in os.walk(qml):
    for f in sorted(fs):
        if not f.endswith('.qml'): continue
        p = os.path.join(dp, f)
        for n, line in enumerate(open(p, encoding='utf-8'), 1):
            for m in KEY.finditer(line): keys.add(m.group(1).encode().decode('unicode_escape'))
            for rx in (PROP, FIELD):
                for m in rx.finditer(line):
                    before = line[:m.start(2)]
                    if 'i18n.t(' in before[-12 - len(m.group(1)):] or not visible(m.group(2)): continue
                    if rx is FIELD and not re.search(r'[\[{,]\s*$', line[:m.start()].rstrip()[-1:] or '') and '{' not in line[:m.start()]: continue
                    unwrapped.append((os.path.relpath(p, root), n, m.group(1), m.group(2)))
for u in unwrapped: print('%s:%d  %s: "%s"' % u)
print('UNWRAPPED', len(unwrapped), file=sys.stderr)
if '--keys' in sys.argv:
    known = set(json.load(open(os.path.join(root, 'i18n', 'ui-keys.json'))))
    miss = sorted(keys - known); print('KEYS NOT IN ui-keys.json', len(miss), file=sys.stderr)
    if '--write-missing' in sys.argv: json.dump(miss, open(sys.argv[-1], 'w'), ensure_ascii=False, indent=0)
