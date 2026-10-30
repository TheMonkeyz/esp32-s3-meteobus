#!/bin/bash
# flash.sh MD5 [SECS]: check p4/firmware/meteobus.bin's md5, stop a running log, request a flash, wait until it logs
cd "$(dirname "$0")"
[ "$(md5sum firmware/meteobus.bin | cut -c1-32)" = "$1" ] || { echo "MD5 MISMATCH"; exit 1; }
if [ "$(tr -d "\r\n" < flash.status)" = logging ]; then
  echo > stop.request
  for i in $(seq 1 15); do s=$(tr -d "\r\n" < flash.status); [ "$s" != logging ] && break; sleep 1; done
  [ "$(tr -d "\r\n" < flash.status)" != logging ] || { echo "HELPER DID NOT STOP"; exit 2; }
fi
n=$(grep -c "FLASH OK" flash_helper.log)
echo "${2:-1800}" > flash.request
for i in $(seq 1 30); do [ "$(grep -c 'FLASH OK' flash_helper.log)" -gt "$n" ] && break; grep -qi "failed" flash.status && break; sleep 3; done
tail -1 flash_helper.log
