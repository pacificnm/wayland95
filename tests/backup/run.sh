#!/bin/bash
# Tests session/wayland95-backup (the Backup control panel's engine) on its
# own: a made-up home folder, a folder standing in for the drive, and
# stand-ins for findmnt, udisksctl and win95notify (tests/backup/fakes).
# Nothing of this computer's is read or written.
#
#   tests/backup/run.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
WORK=$(mktemp -d /tmp/wayland95-backup.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
export HOME=$WORK/home XDG_CONFIG_HOME=$WORK/home/.config XDG_STATE_HOME=$WORK/home/.local/state
export PATH=$HERE/fakes:$PATH W95_FAKE_DRIVE=$WORK/drive
STATE=$XDG_STATE_HOME/wayland95
CONF=$XDG_CONFIG_HOME/wayland95/backup.conf
DEST="$WORK/drive/Wayland 95 Backups/me"
mkdir -p "$HOME/Documents/letters" "$HOME/My Pictures" "$HOME/Documents/.cache" "$(dirname "$CONF")"
echo "dear sam" >"$HOME/Documents/letters/sam.txt"
echo "notes" >"$HOME/Documents/notes.txt"
echo "junk" >"$HOME/Documents/.cache/junk"
echo "pixels" >"$HOME/My Pictures/cat.png"

pass=0 fail=0
ok() { echo "PASS  $1"; pass=$((pass + 1)); }
bad() { echo "FAIL  $1"; [ -n "${2:-}" ] && echo "      $2"; fail=$((fail + 1)); }
expect() { if [ "$1" -eq 0 ]; then ok "$2"; else bad "$2" "${3:-}"; fi; }
backup() { "$REPO/session/wayland95-backup" "$@"; }
st() { sed -n "s/^$1=//p" "$STATE/backup.status" | head -n 1; }
snapshots() { ls -1d "$DEST"/20*_* 2>/dev/null | grep -v partial; }
conf() {
    {
        echo "DRIVE_UUID=test-uuid"
        echo "DRIVE_NAME=Test Drive"
        echo "DEST_DIR=Wayland 95 Backups/me"
        echo "KEEP=${KEEP:-10}"
        echo "SCHEDULE=${SCHEDULE:-daily}"
        echo "FOLDER=$HOME/Documents"
        echo "FOLDER=$HOME/My Pictures"
        echo "EXCLUDE=.cache/"
    } >"$CONF"
}

echo "== wayland95-backup"
out=$(backup --now 2>&1); code=$?
[ $code -eq 2 ]; expect $? "not set up: it says so" "$code: $out"

conf
out=$(backup --scheduled 2>&1); code=$?
[ $code -eq 3 ] && [ "$(st result)" = skipped ] && [ "$(st message)" = "Test Drive isn't plugged in." ]
expect $? "the drive not plugged in: skipped, and the status says why" "$code: $out; $(cat "$STATE/backup.status")"
! grep -q "^notify" "$W95_FAKE_DRIVE.log" 2>/dev/null; expect $? "(no notification: it hasn't been a week)" "$(cat "$W95_FAKE_DRIVE.log")"

: >"$W95_FAKE_DRIVE.present"; : >"$W95_FAKE_DRIVE.nomount"
out=$(backup --scheduled 2>&1); code=$?
[ $code -eq 3 ] && [ "$(st result)" = skipped ]; expect $? "plugged in but it can't be opened (cron): skipped" "$code: $out"
rm -f "$W95_FAKE_DRIVE.nomount"

out=$(backup --now 2>&1); code=$?
first=$(snapshots | head -n 1)
[ $code -eq 0 ] && [ "$(st result)" = ok ] && [ -n "$first" ]
expect $? "plugged in: it's opened (udisksctl mount) and backed up into a dated folder" "$code: $out; $(cat "$STATE/backup.status")"
grep -q "udisksctl mount --no-user-interaction -b /dev/disk/by-uuid/test-uuid" "$W95_FAKE_DRIVE.log"
expect $? "(mounted without asking: a password box can't come up from cron)" "$(cat "$W95_FAKE_DRIVE.log")"
[ "$(cat "$first$HOME/Documents/letters/sam.txt")" = "dear sam" ] && [ -f "$first$HOME/My Pictures/cat.png" ]
expect $? "the folders are in it, under their full paths (a name with a space too)" "$(cd "$first" && find . | head)"
[ ! -e "$first$HOME/Documents/.cache" ]; expect $? "what's left out (.cache/) isn't" "$(ls -a "$first$HOME/Documents")"
[ "$(readlink "$DEST/Latest")" = "$(basename "$first")" ]; expect $? "Latest points at it" "$(ls -l "$DEST")"
[ "$(st files)" = 3 ] && [ "$(st folder)" = "$first" ]; expect $? "the status: 3 files, and where" "$(cat "$STATE/backup.status")"
echo "$out" | grep -q "%"; expect $? "(it prints progress for the applet)" "$out"

sleep 1
echo "dear sam, again" >"$HOME/Documents/letters/sam.txt"
backup --now >/dev/null 2>&1
second=$(snapshots | tail -n 1)
[ "$second" != "$first" ] && [ "$(cat "$second$HOME/Documents/letters/sam.txt")" = "dear sam, again" ] \
    && [ "$(cat "$first$HOME/Documents/letters/sam.txt")" = "dear sam" ]
expect $? "a second backup: a folder of its own, the first kept as it was" "$(snapshots)"
[ "$(stat -c %i "$first$HOME/Documents/notes.txt")" = "$(stat -c %i "$second$HOME/Documents/notes.txt")" ]
expect $? "unchanged files are shared between them (hard links: no extra space)" ""
[ "$(st files)" = 1 ]; expect $? "(and only the changed one was copied)" "$(st files)"

KEEP=2 conf
sleep 1; backup --now >/dev/null 2>&1
[ "$(snapshots | wc -l)" = 2 ] && ! snapshots | grep -q "$(basename "$first")"
expect $? "Keep 2: the oldest goes" "$(snapshots)"

out=$(backup --due 2>&1)
[ "$out" = "Not due yet." ] && [ "$(snapshots | wc -l)" = 2 ]; expect $? "--due (the drive plugged in): nothing if the last is recent" "$out"
sed -i 's/^last_ok=.*/last_ok=1/' "$STATE/backup.status"
sleep 1; out=$(backup --due 2>&1)
[ "$(st result)" = ok ] && [ "$(sed -n 's/^last_ok=//p' "$STATE/backup.status")" != 1 ]
expect $? "--due: a backup when the last is older than the schedule" "$out"

# a week without one, the drive missing: a notification, once a day
rm -f "$W95_FAKE_DRIVE.present" "$W95_FAKE_DRIVE.mounted"
sed -i 's/^last_ok=.*/last_ok=1/' "$STATE/backup.status"
backup --scheduled >/dev/null 2>&1; backup --scheduled >/dev/null 2>&1
[ "$(grep -c "^notify.*No backup for a week" "$W95_FAKE_DRIVE.log")" = 1 ]
expect $? "a week without a backup: a notification (once a day)" "$(grep notify "$W95_FAKE_DRIVE.log")"

# a drive it can't write to
: >"$W95_FAKE_DRIVE.present"; : >"$W95_FAKE_DRIVE.mounted"
chmod 555 "$DEST"
out=$(backup --now 2>&1); code=$?
chmod 755 "$DEST"
[ $code -eq 1 ] && [ "$(st result)" = failed ] && grep -q "^notify.*-u critical Backup failed" "$W95_FAKE_DRIVE.log"
expect $? "a drive it can't write to: failed, with a notification" "$code: $out; $(st message)"
[ "$(sed -n 's/^last_ok_folder=//p' "$STATE/backup.status")" != "" ]; expect $? "(the last good one stays known)" "$(cat "$STATE/backup.status")"

# one at a time
exec 8>"$STATE/backup.lock"; flock 8
out=$(backup --now 2>&1)
flock -u 8; exec 8>&-
[ "$out" = "A backup is running already." ]; expect $? "two at once: the second leaves it to the first" "$out"

# a drive without hard links (FAT): one mirror, what's replaced kept aside
rm -rf "$DEST"
W95_BACKUP_NO_LINKS=1 backup --now >/dev/null 2>&1
[ -f "$DEST/Mirror$HOME/Documents/notes.txt" ] && [ -z "$(snapshots)" ]; expect $? "no hard links: one copy, Mirror" "$(ls "$DEST")"
rm "$HOME/Documents/notes.txt"; echo "changed" >"$HOME/My Pictures/cat.png"
sleep 1; W95_BACKUP_NO_LINKS=1 backup --now >/dev/null 2>&1
[ ! -e "$DEST/Mirror$HOME/Documents/notes.txt" ] && [ -f "$(ls -d "$DEST"/Deleted/*/ | head -n 1)${HOME#/}/Documents/notes.txt" ] \
    && [ "$(cat "$DEST/Mirror$HOME/My Pictures/cat.png")" = changed ]
expect $? "a file deleted or replaced since: moved to Deleted/DATE, not lost" "$(cd "$DEST" && find . | head -20)"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
