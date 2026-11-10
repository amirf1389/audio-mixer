#!/usr/bin/env python3
# Keeps native/amixos/CMakeLists.txt in step with the interface tables present in native/amixos/i18n/ui/ and reports each table's coverage.
import json, os, re
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'native', 'amixos'); keys = json.load(open(os.path.join(root, 'i18n', 'ui-keys.json')))
order = "fa ar ur es fr de pt ru tr hi zh ja ko ms id it nl pl vi th uk".split(); have = []
for l in order:
    p = os.path.join(root, 'i18n', 'ui', l + '.json')
    try: d = json.load(open(p))
    except Exception: continue
    for k in list(d):
        if k not in keys or not isinstance(d[k], str) or not d[k]: del d[k]; continue
        d[k] = re.match(r'^\s*', k).group(0) + d[k].strip() + re.search(r'\s*$', k).group(0)   # the key's own leading / trailing whitespace
    json.dump(d, open(p, 'w'), ensure_ascii=False, indent=0); have.append(l); print(l, len(d), 'of', len(keys))
cm = open(os.path.join(root, 'CMakeLists.txt')).read()
cm = re.sub(r'\n        i18n/ui/[a-z]+\.json', '', cm).replace('        i18n/auto-58.json', '        i18n/auto-58.json' + ''.join('\n        i18n/ui/%s.json' % l for l in have))
open(os.path.join(root, 'CMakeLists.txt'), 'w').write(cm)
