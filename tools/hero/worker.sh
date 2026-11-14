#!/bin/bash
# a render worker that survives browser crashes: batches of 40 frames, resumed until its range is complete
cd "$(dirname "$0")"
while true; do OFF=msaa MAXN=40 node render.js "$1" "$2" frames4k; rc=$?; [ $rc -eq 0 ] && break; sleep 2; done
echo "WORKER $1-$2 COMPLETE"
