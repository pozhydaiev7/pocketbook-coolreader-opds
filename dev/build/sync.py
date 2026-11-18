#!/usr/bin/env python3
"""Copies a git clone (a Windows working tree, CRLF line ends) into a build tree of WSL with LF
line ends. Only the files changed since the last run are written, so make rebuilds only what
changed. Files removed from the clone are removed from the tree.

    python3 sync.py <clone> <build tree>
"""
import json
import os
import sys

SRC = os.path.abspath(sys.argv[1])
DST = os.path.abspath(os.path.expanduser(sys.argv[2]))
MANIFEST = DST + '.manifest.json'
# the build folders of the device targets lie in the tree itself
SKIP_TOP = {'.git', 'pbpro5', 'pb360', 'pbpro2', 'pbpro4'}

try:
    manifest = json.load(open(MANIFEST))
except Exception:
    manifest = {}
seen = set()
written = 0
for root, dirs, files in os.walk(SRC):
    rel_root = os.path.relpath(root, SRC)
    if rel_root == '.':
        dirs[:] = [d for d in dirs if d not in SKIP_TOP]
        rel_root = ''
    for name in files:
        rel = os.path.join(rel_root, name)
        if rel == '.git':       # the pointer file of a git worktree
            continue
        src = os.path.join(root, name)
        dst = os.path.join(DST, rel)
        st = os.stat(src)
        key = [st.st_size, st.st_mtime_ns]
        seen.add(rel)
        if manifest.get(rel) == key and os.path.exists(dst):
            continue
        data = open(src, 'rb').read()
        if b'\0' not in data and b'\r\n' in data:
            data = data.replace(b'\r\n', b'\n')
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        open(dst, 'wb').write(data)
        os.chmod(dst, 0o755 if name.endswith(('.sh', '.app', '.py')) or b'#!' == data[:2] else 0o644)
        manifest[rel] = key
        written += 1
removed = 0
for rel in list(manifest):
    if rel not in seen:
        try:
            os.remove(os.path.join(DST, rel))
        except OSError:
            pass
        del manifest[rel]
        removed += 1
json.dump(manifest, open(MANIFEST, 'w'))
print('sync: %d written, %d removed, %d files' % (written, removed, len(seen)))
