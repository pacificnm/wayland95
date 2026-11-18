#!/bin/bash
# Makes the Wayland Studio manual's pictures (docs/studio-manual/images/):
# the Studio and the Book Club sample in the test desktop (tests/run.sh's
# nested compositor, at 1280x800), step by step. Also writes the finished
# sample (sample/bookclub.w95proj and its bookclub.db).
#
#   docs/studio-manual/screenshots.sh          (after a build)
#   KEEP=1 docs/studio-manual/screenshots.sh   (keep the full screenshots)
set -u
MANUAL=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$MANUAL/../.." && pwd)
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/studio-manual.XXXXXX")
# tests/run.sh's helpers (everything before its list of suites), and the steps below
HARNESS=$REPO/tests/.studio-manual-shots.sh
sed '/^ALL=(/,$d' "$REPO/tests/run.sh" >"$HARNESS"
cat "$MANUAL/screenshots.steps" >>"$HARNESS"
echo 'suite_manual; echo "WORK=$WORK"' >>"$HARNESS"
MANUAL=$MANUAL SCRATCH=$SCRATCH bash "$HARNESS"
status=$?
rm -f "$HARNESS"
rm -rf "$SCRATCH"
exit $status
