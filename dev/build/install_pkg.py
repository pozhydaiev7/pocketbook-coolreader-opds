"""Copies system/bin and system/share/cr3 of a package zip to the reader and compares every file.
install.sh and package.version of the zip root are for the update over the network and are not copied.
Usage: python -I install_pkg.py <zip> <drive root, e.g. D:/>"""
import os, sys, zipfile

zip_path, root = sys.argv[1], sys.argv[2]
z = zipfile.ZipFile(zip_path)
names = [n for n in z.namelist() if not n.endswith('/')]
assert not any(n.startswith('system/config') or n.startswith('system/state') for n in names), 'settings in the zip'
root_files = [n for n in names if '/' not in n]
assert sorted(root_files) == ['install.sh', 'package.version'], root_files
names = [n for n in names if '/' in n]
other = [n for n in names if not (n.startswith('system/bin/') or n.startswith('system/share/cr3/'))]
assert not other, other
listed = z.read('system/share/cr3/package.files').decode('utf-8').split('\n')
listed = sorted(l for l in listed if l)
in_zip = sorted(n[len('system/share/cr3/'):] for n in names if n.startswith('system/share/cr3/'))
assert listed == in_zip, 'package.files differs from the zip'
for n in names:
    dst = os.path.join(root, *n.split('/'))
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, 'wb') as f:
        f.write(z.read(n))
bad = []
for n in names:
    with open(os.path.join(root, *n.split('/')), 'rb') as f:
        if f.read() != z.read(n):
            bad.append(n)
print('written', len(names), 'differ', len(bad), bad[:5])
print(z.read('package.version').decode('utf-8').strip())
share = os.path.join(root, 'system', 'share', 'cr3')
print('crlog.ini on device:', os.path.exists(os.path.join(share, 'crlog.ini')))
print('package.files on device:', len(listed), 'files listed')
# files of the device folder that the package does not bring: the files of the user and leftovers
extra = []
for base, dirs, files in os.walk(share):
    rel_base = os.path.relpath(base, share).replace('\\', '/')
    if rel_base == '.cache' or rel_base.startswith('.cache/'):
        continue
    for name in files:
        rel = name if rel_base == '.' else rel_base + '/' + name
        if rel not in listed:
            extra.append(rel)
print('not from the package (kept by an update):', len(extra), sorted(extra)[:20])
for name in ('latest.zip', 'latest.zip.part', '.cr3update', '.cr3restart'):
    if os.path.exists(os.path.join(root, name)):
        print('NOTE: %s is on the device' % name)
for name in ('.cr3updated', '.cr3rollback'):
    if os.path.exists(os.path.join(root, 'system', name)):
        print('NOTE: system/%s is on the device' % name)
for name in ('cr3.old', 'cr3.failed'):
    if os.path.exists(os.path.join(root, 'system', 'share', name)):
        print('NOTE: system/share/%s is on the device' % name)
