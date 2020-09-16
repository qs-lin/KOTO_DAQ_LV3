rm -f output_events_*
nListen=$1
nWrite=$2
nEvents=$3
#!/bin/sh

if [ -d .recv_ready ]; then
	rmdir .recv_ready
fi
./collect $nListen $nWrite &
COL_PID="$!"
echo "collect PID is $COL_PID"
./generate $nEvents $nListen
kill -2 $COL_PID
#sleep 2 # sleep while collect shuts down
sleep 4 # sleep while collect shuts down
