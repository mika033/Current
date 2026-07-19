#!/usr/bin/env bash
# Headless screenshot of the redesigned Random module window.
# Launches the Standalone under Xvfb, drags the Random chip onto the canvas,
# double-clicks the node to open its settings window, and captures the screen.
set -u
BIN="/home/user/Current/build-linux/Current_artefacts/Release/Standalone/Current"
OUT="${1:-/tmp/random_window.png}"

pkill -f "Current_artefacts.*Standalone/Current" 2>/dev/null
pkill Xvfb 2>/dev/null
sleep 1

Xvfb :99 -screen 0 1200x800x24 >/tmp/xvfb.log 2>&1 &
sleep 2
export DISPLAY=:99

"$BIN" >/tmp/standalone.log 2>&1 &
APP_PID=$!
sleep 5

WID=$(xdotool search --name "Current" | head -1)
if [ -z "$WID" ]; then echo "NO_WINDOW"; cat /tmp/standalone.log; exit 1; fi
xdotool windowmove "$WID" 0 0
xdotool windowsize "$WID" 900 620
sleep 1

# Window-local coordinates (see MainView layout): Random chip is the leftmost
# palette item along the bottom; canvas centre is where we drop it.
CHIP_X=51;  CHIP_Y=560
DROP_X=450; DROP_Y=300

# Drag-and-drop: press on the chip, nudge past the drag threshold, walk to the
# canvas, release. JUCE starts the drag on the first move after mousedown.
xdotool mousemove $CHIP_X $CHIP_Y
xdotool mousedown 1
for step in 1 2 3 4 5 6 7 8; do
  cx=$(( CHIP_X + (DROP_X - CHIP_X) * step / 8 ))
  cy=$(( CHIP_Y + (DROP_Y - CHIP_Y) * step / 8 ))
  xdotool mousemove $cx $cy
  sleep 0.1
done
xdotool mouseup 1
sleep 1

# Double-click the dropped node to open its settings window.
xdotool mousemove $DROP_X $DROP_Y
xdotool click --repeat 2 --delay 120 1
sleep 1

import -window root "$OUT"
echo "SHOT: $OUT"
kill $APP_PID 2>/dev/null
pkill Xvfb 2>/dev/null
