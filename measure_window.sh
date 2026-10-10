#!/usr/bin/env bash
exec > >(tee results.txt) 2>&1

APP=./build/release/myapp
run=0

for window in 1 16; do
  for loss in 0 0.05; do
    for trial in 1 2 3; do
      run=$((run + 1))
      session="mib-$run"
      received="received-$session.bin"

      echo "window=$window loss=$loss trial=$trial"
      "$APP" recv -s "$session" 127.0.0.1 "$received" &
      recv_pid=$!
      sleep 0.2

      time "$APP" send -s "$session" -w "$window" \
        -l "$loss" -c 0 -d 0 127.0.0.1 1mib.bin

      wait "$recv_pid"
      if cmp --silent 1mib.bin "$received"; then
        echo "Copy verified: $received"
      else
        echo "Error: Files do not match for $received!" >&2
        exit 1
      fi
      echo -e "\n------------------------------------------------\n"
    done
  done
done