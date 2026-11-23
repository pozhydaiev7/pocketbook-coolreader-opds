# CoolReader 3 for PocketBook (OPDS fork)

Read `dev/README.md` before anything else: where the code is, how to set up a new machine (WSL,
the SDK), how a package is built, checked and put on a reader, the PC bench, and the rules of the
owner. For PDF and DjVu read `dev/pdfdjvu/STATUS.md` next: the state of that work.

The short form of the rules:

- Talk to the owner in Russian; code, comments, commits, changelog and docs are English.
- Commit, push, tag, release: only on his word, each time. One dense subject line per commit.
- Build a device package and touch the reader only on his word, and only when all he listed is done.
- WSL builds: announce, at most 4 jobs with `nice`, `wsl.exe --shutdown` afterwards.
- Every feature: strings in `cr3gui/po`, the three manuals, an entry in `CHANGELOG.md`.
- Speed over beauty of the code; measure on the bench (`dev/bench`) before and after.
- One line of versions, in `master`: `0.0.8-opds` is released, test builds are `-alphaN`.
