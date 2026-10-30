#!/bin/bash
# swipes.sh: a few test swipes through the console, then the drag log lines
cd "$(dirname "$0")"
for c in "fps reset" "swipe left" "swipe right" "swipe left" "swipe right" "swipe down" "swipe up"; do echo "$c" > serial.send; sleep 3; done
grep -E "slide: |rst:|Guru|PPA" serial_live.txt | tail -8
