#!/bin/sh
#
# The switch between CoolReader and the reading programs of the firmware: an item of the
# Applications screen. A tap moves the file associations of the user aside
# (system/config/extensions.cfg, which names CoolReader first), so books open in the programs of
# the firmware; the next tap puts them back. With neither file on the reader it does nothing.
#

CFG_FILE="/mnt/ext1/system/config/extensions.cfg"
BAK_FILE="/mnt/ext1/system/config/extensions.bak"

# Config save
if [ -f "$CFG_FILE" ]; then
	cp -f "$CFG_FILE" "$BAK_FILE"
	rm -f "$CFG_FILE"
	sync
	/ebrmain/bin/iv2sh SendEventTo -1 154
	exit
fi

# Backup restore
if [ -f "$BAK_FILE" ]; then
	cp -f "$BAK_FILE" "$CFG_FILE"
	rm -f "$BAK_FILE"
	sync
	/ebrmain/bin/iv2sh SendEventTo -1 154
	exit
fi

exit
