# check_pkg.py <zip> <version string> [text...]: the binary of the package has the version string
# and every other text given
import sys, zipfile

z = zipfile.ZipFile(sys.argv[1])
data = z.read('system/share/cr3/bin/cr3-pb.app')
print(sys.argv[1].replace('\\', '/').split('/')[-1], len(z.namelist()), 'entries, binary', len(data), 'bytes')
for text in sys.argv[2:]:
    print('  %-60s %s' % (text, 'found' if text.encode('utf-8') in data else 'MISSING'))
