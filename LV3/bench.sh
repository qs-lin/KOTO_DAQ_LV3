#!/bin/sh
rm -f /local/s2/toyDAQ/* 
if [ -d .recv_ready ]; then
	rmdir .recv_ready
fi
./collect $2 &
COL_PID="$!"
echo "collect PID is $COL_PID"
./generate $1 $2 $3
kill -2 $COL_PID
sleep 2 # sleep while collect shuts down
