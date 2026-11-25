#!/bin/sh

# Firmware 6.11 refuses to load system libraries that request an executable stack
# (libinkview.so, libhwconfig.so) into third-party apps; this glibc tunable allows them again.
# Older firmware ignores it.
export GLIBC_TUNABLES=glibc.rtld.execstack=2

# CR3_WORK_DIR: another root folder, for the tests of the update on a PC
WORK_DIR="${CR3_WORK_DIR:-/mnt/ext1}"
SELF="$WORK_DIR/system/bin/cr3-pb.app"
RESTART_MARK="$WORK_DIR/.cr3restart"
UPDATE_PACKAGE="$WORK_DIR/latest.zip"
TEMP_DIR="$WORK_DIR/.cr3update"
INSTALLER="$TEMP_DIR/install.sh"
CR3_DIR="$WORK_DIR/system/share/cr3"
CR3_BIN="$CR3_DIR/bin/cr3-pb.app"
# Update over the network (see install.sh of the package):
# the previous version, kept until the new one shows its first screen
OLD_DIR="$WORK_DIR/system/share/cr3.old"
# a version that did not start
FAILED_DIR="$WORK_DIR/system/share/cr3.failed"
# written by the installer, removed by CR3 at its first screen
UPDATE_MARKER="$WORK_DIR/system/.cr3updated"
# the update failed: CR3 tells the user
FAILED_MARKER="$WORK_DIR/system/.cr3rollback"

# Moves the files of the user from the folder $1 to the folder $2: the book cache, the fonts and
# every file that the package of $1 did not bring (package.files of $1 lists the package).
# The same function is in install.sh.
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

# CD to working dir
cd "$WORK_DIR"

# Remove temporary dir if present
if [ -d "$TEMP_DIR" ]; then
    rm -rf "$TEMP_DIR"
fi

# If there's an update package
if [ -f "$UPDATE_PACKAGE" ]; then

    # Create temp dir
    mkdir -p "$TEMP_DIR"

    # Unzip package
    unzip "$UPDATE_PACKAGE" -d "$TEMP_DIR"

    # Remove the update package
    rm -f "$UPDATE_PACKAGE"

    # If the update package has been removed and there's an installer
    if [ -f "$INSTALLER" -a ! -f "$UPDATE_PACKAGE" ]; then

        # Launch installer: it keeps the installed version in $OLD_DIR
        sh "$INSTALLER" "$1"

        # Stop script. It's relaunched from the installer.
        exit
    fi

    # Not an update package: the installed version stays, CR3 tells the user
    rm -rf "$TEMP_DIR"
    echo "" > "$FAILED_MARKER"
fi

# The previous version is not needed after the new one has shown its first screen
if [ ! -f "$UPDATE_MARKER" ]; then
    if [ -d "$OLD_DIR" -o -f "$SELF.old" ]; then
        ( rm -rf "$OLD_DIR"; rm -f "$SELF.old" ) &
    fi
fi
if [ -d "$FAILED_DIR" ]; then
    rm -rf "$FAILED_DIR" &
fi

# One copy of the program at a time. The firmware starts this script for every book that is
# tapped, also while the program runs in the background with another book. The copy that runs
# names its task and its process in a file; it is left the book to open, given the screen by
# the tool of the firmware, and this start ends (KOReader does the same on these readers).
# Without the tool the program itself does it, a moment later. Not in the tests on a PC.
INSTANCE_FILE=/tmp/cr3-pb.instance
OPEN_FILE=/tmp/cr3-pb.open
if [ -z "$CR3_WORK_DIR" ] && [ -f "$INSTANCE_FILE" ]; then
    read INSTANCE_TASK INSTANCE_PID < "$INSTANCE_FILE"
    IV2SH=/usr/bin/iv2sh
    [ -x "$IV2SH" ] || IV2SH=/ebrmain/bin/iv2sh
    case "x$INSTANCE_TASK$INSTANCE_PID" in
        x|*[!0-9x]*) INSTANCE_PID= ;;
    esac
    if [ -n "$INSTANCE_TASK" ] && [ -n "$INSTANCE_PID" ] && [ -x "$IV2SH" ] \
            && grep -q "cr3-pb.app" "/proc/$INSTANCE_PID/cmdline" 2>/dev/null; then
        if printf '%s\n' "$1" > "$OPEN_FILE.new" && mv "$OPEN_FILE.new" "$OPEN_FILE"; then
            if [ -f "$CR3_DIR/crlog.ini" ]; then
                echo "`date '+%Y/%m/%d %H:%M:%S'`.0000 INFO one copy: the launcher $$ gives the book $1 to" \
                    "the running copy (task $INSTANCE_TASK, process $INSTANCE_PID) with $IV2SH and ends" \
                    >> "$WORK_DIR/cr3log.copies.txt"
            fi
            exec "$IV2SH" SetActiveTask "$INSTANCE_TASK" 0
        fi
    fi
    # the book is opened by this start: no request is left behind
    rm -f "$OPEN_FILE" "$OPEN_FILE.new"
fi

# Keep starting time
startedTime="`date +"%s"`"

# Launch CR3
#"$CR3_BIN" "$1" > $WORK_DIR/system/cr3-pb.normal.log 2> $WORK_DIR/system/cr3-pb.crash.log
"$CR3_BIN" "$1"
status=$?

# Stopped time
stoppedTime="`date +"%s"`"
runTime=$(($stoppedTime-$startedTime))

# A new version that stopped at once without its first screen: back to the previous version.
# Exit codes 1 and 2 are "no book" and "the book cannot be opened", not a broken version.
if [ -f "$UPDATE_MARKER" -a -d "$OLD_DIR" ] && [ $runTime -lt 10 ] && [ $status -gt 2 ]; then
    failedVersion="`cat "$UPDATE_MARKER"`"
    rm -rf "$FAILED_DIR"
    if mv "$CR3_DIR" "$FAILED_DIR"; then
        if mv "$OLD_DIR" "$CR3_DIR"; then
            carry_user_files "$FAILED_DIR" "$CR3_DIR"
            rm -f "$UPDATE_MARKER" "$RESTART_MARK"
            echo "$failedVersion" > "$FAILED_MARKER"
            if [ -f "$SELF.old" ]; then
                rm -f "$SELF"
                mv "$SELF.old" "$SELF"
            fi
            sh "$SELF" "$1" &
            exit
        fi
        mv "$FAILED_DIR" "$CR3_DIR"
    fi
fi

# If restart required
if [ -f "$RESTART_MARK" ]; then

    # Remove marker
    rm -f "$RESTART_MARK"

    # Relaunch this script
    sh "$SELF" "$1" &

    # Stop script
    exit
fi
