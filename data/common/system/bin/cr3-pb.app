#!/bin/sh

# Firmware 6.11 refuses to load system libraries that request an executable stack
# (libinkview.so, libhwconfig.so) into third-party apps; this glibc tunable allows them again.
# Older firmware ignores it.
export GLIBC_TUNABLES=glibc.rtld.execstack=2

WORK_DIR="/mnt/ext1"
RESTART_MARK="$WORK_DIR/.cr3restart"
UPDATE_PACKAGE="$WORK_DIR/latest.zip"
TEMP_DIR="$WORK_DIR/.cr3update"
INSTALLER="$TEMP_DIR/install.sh"
CR3_DIR="$WORK_DIR/system/share/cr3"
CR3_BIN="$CR3_DIR/bin/cr3-pb.app"

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

        # Full backup. It'll be done at some point

        # Launch installer
        sh "$INSTALLER" "$1"

        # Stop script. It's relaunched from the installer.
        exit
    fi
fi

# Keep starting time
startedTime="`date +"%s"`"

# Launch CR3
#"$CR3_BIN" "$1" > $WORK_DIR/system/cr3-pb.normal.log 2> $WORK_DIR/system/cr3-pb.crash.log
"$CR3_BIN" "$1"

# Stopped time
stoppedTime="`date +"%s"`"

# If restart required
if [ -f "$RESTART_MARK" ]; then

    # Remove marker
    rm -f "$RESTART_MARK"

    # Relaunch this script
    sh $WORK_DIR/system/bin/cr3-pb.app "$1" &

    # Stop script
    exit
fi

# Try detecting crash (less than 5 seconds of running time)
runTime=$(($stoppedTime-$startedTime))
if [ $runTime -lt 5 ]; then
    # Restore backup if needed (will be done at some point)
    :
fi
