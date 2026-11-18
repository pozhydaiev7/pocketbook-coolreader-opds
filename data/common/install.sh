#!/bin/sh

#
# Installer of the update package (update over the network).
# If you install the package by hand, ignore this file: copy the "system" folder to the device.
#
# The launcher (system/bin/cr3-pb.app) finds /mnt/ext1/latest.zip, unpacks it to
# /mnt/ext1/.cr3update and runs this file from there.
#
# What it does:
#  - checks that the package was unpacked whole (system/share/cr3/package.files lists its files);
#  - moves the installed version aside (system/share/cr3.old) and puts the new one in its place;
#  - moves the files of the user to the new folder: the book cache, the fonts and every file the
#    old package did not bring (crlog.ini, own skins and so on);
#  - replaces the file associations (system/config/extensions.cfg) and the switch of the
#    Applications screen with those of the package;
#  - opens nothing else of system/config and nothing of system/state: the settings, the history
#    and the statistics.
# When the new version does not start, the launcher brings system/share/cr3.old back.
#

# CR3_WORK_DIR: another root folder, for the tests of the update on a PC
WORK_DIR="${CR3_WORK_DIR:-/mnt/ext1}"
TEMP_DIR="$WORK_DIR/.cr3update"
UPDATE_PACKAGE="$WORK_DIR/latest.zip"
UPDATE_MARKER="$WORK_DIR/system/.cr3updated"
FAILED_MARKER="$WORK_DIR/system/.cr3rollback"
LAUNCHER="$WORK_DIR/system/bin/cr3-pb.app"
CR3_DIR="$WORK_DIR/system/share/cr3"
OLD_DIR="$WORK_DIR/system/share/cr3.old"
NEW_DIR="$TEMP_DIR/system/share/cr3"
NEW_LAUNCHER="$TEMP_DIR/system/bin/cr3-pb.app"
BOOK="$1"

if [ ! -d "$WORK_DIR" ]; then
	echo "Your directory structure is unknown!"
	exit 1
fi

if [ ! -f "$TEMP_DIR/install.sh" ]; then
	echo "You probably launched this manually."
	echo "You shouldn't!"
	exit 1
fi

cd "$WORK_DIR" || exit 1

# version of the package: the line "Version: <version>" of package.version
VERSION=""
if [ -f "$TEMP_DIR/package.version" ]; then
	while read -r key value rest; do
		if [ "$key" = "Version:" ]; then
			VERSION="$value"
			break
		fi
	done < "$TEMP_DIR/package.version"
fi

# Moves the files of the user from the folder $1 to the folder $2: the book cache, the fonts and
# every file that the package of $1 did not bring (package.files of $1 lists the package).
# The same function is in the launcher.
carry_user_files() {
	for name in .cache fonts; do
		if [ -e "$1/$name" ] && [ ! -e "$2/$name" ]; then
			mv "$1/$name" "$2/$name"
		fi
	done
	if [ -f "$1/package.files" ]; then
		( cd "$1" && find . -type f ) | while IFS= read -r f; do
			f="${f#./}"
			grep -qxF -e "$f" "$1/package.files" && continue
			[ -e "$2/$f" ] && continue
			case "$f" in
				*/*) mkdir -p "$2/${f%/*}" ;;
			esac
			mv "$1/$f" "$2/$f"
		done
	else
		# a version without the list: the known files of the user
		for name in crlog.ini cr3.ini; do
			if [ -f "$1/$name" ] && [ ! -e "$2/$name" ]; then
				mv "$1/$name" "$2/$name"
			fi
		done
	fi
}

# The installed version stays: tells CR3 about the failure and starts it again
fail() {
	echo "Update failed: $1"
	echo "$VERSION" > "$FAILED_MARKER"
	rm -rf "$TEMP_DIR"
	rm -f "$UPDATE_PACKAGE"
	sh "$LAUNCHER" "$BOOK" &
	exit 1
}

# the package is whole
[ -f "$NEW_LAUNCHER" ] || fail "no launcher in the package"
[ -f "$NEW_DIR/bin/cr3-pb.app" ] || fail "no program in the package"
[ -f "$NEW_DIR/package.files" ] || fail "no list of files in the package"
while IFS= read -r f; do
	[ -z "$f" ] && continue
	[ -f "$NEW_DIR/$f" ] || fail "the package is not whole: $f"
done < "$NEW_DIR/package.files"

# the installed version moves aside, the new one takes its place
rm -rf "$OLD_DIR"
rm -f "$LAUNCHER.old"
if [ -d "$CR3_DIR" ]; then
	mv "$CR3_DIR" "$OLD_DIR" || fail "cannot move the installed version aside"
fi
if ! mv "$NEW_DIR" "$CR3_DIR"; then
	[ -d "$OLD_DIR" ] && mv "$OLD_DIR" "$CR3_DIR"
	fail "cannot put the new version in place"
fi

# files of the user
if [ -d "$OLD_DIR" ]; then
	carry_user_files "$OLD_DIR" "$CR3_DIR"
fi

# launcher: the old one is kept for the way back
if [ -f "$LAUNCHER" ]; then
	mv "$LAUNCHER" "$LAUNCHER.old"
fi
if ! mv "$NEW_LAUNCHER" "$LAUNCHER"; then
	[ -f "$LAUNCHER.old" ] && mv "$LAUNCHER.old" "$LAUNCHER"
fi
chmod 755 "$LAUNCHER" "$CR3_DIR/bin/cr3-pb.app" 2>/dev/null

# The file associations and the switch of the Applications screen of the package replace those
# of the reader, as they do when the package is copied by hand: a new version may open more
# kinds of files. A package of an older version has neither.
# The package spells the folder "system/Config" (dev/build/build_package.sh tells why). On the
# storage of a reader both spellings are one folder; in the tests on a PC they are two.
for associations in "$TEMP_DIR/system/Config/extensions.cfg" "$TEMP_DIR/system/config/extensions.cfg"; do
	if [ -f "$associations" ]; then
		mkdir -p "$WORK_DIR/system/config"
		cp -f "$associations" "$WORK_DIR/system/config/extensions.cfg"
		break
	fi
done
if [ -d "$TEMP_DIR/applications" ]; then
	mkdir -p "$WORK_DIR/applications"
	cp -f "$TEMP_DIR/applications/"*.app "$WORK_DIR/applications/" 2>/dev/null
fi

# remove temporary dir and update package
rm -rf "$TEMP_DIR"
rm -f "$UPDATE_PACKAGE"

# Mark update: CR3 removes the mark at its first screen and shows "Updated to ..."
echo "$VERSION" > "$UPDATE_MARKER"

# relaunch coolreader
sh "$LAUNCHER" "$BOOK" "updated" &
