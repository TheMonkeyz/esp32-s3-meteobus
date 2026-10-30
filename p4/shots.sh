#!/bin/bash
# shots.sh OUT SCREEN...: the display's "shot" command for each screen (in the running log), then the log to OUT
cd "$(dirname "$0")"
for s in "$@"; do
  [ "$s" = "${1}" ] && continue
  n=$(grep -c "^SHOTEND" serial_live.txt)
  echo "shot $s 2" > serial.send
  for i in $(seq 1 40); do [ "$(grep -c '^SHOTEND' serial_live.txt)" -gt "$n" ] && break; sleep 1; done
  echo "$s: $(grep -c '^SHOTEND' serial_live.txt) shots in log"
done
cp serial_live.txt "$1"
