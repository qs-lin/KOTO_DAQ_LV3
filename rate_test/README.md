Requires:

- A C++11 or newer compiler and standard library
- [libcuckoo](https://github.com/efficient/libcuckoo)
- [moodycamel::ConcurrentQueue](https://github.com/cameron314/concurrentqueue)

Build:

	$ make
	/usr/local/bin/clang++ -std=c++11 -O3 -g generate.cpp -o generate
	/usr/local/bin/clang++ -std=c++11 -O3 -g collect.cpp -o collect

Test:

	$ ./bench.sh 5000
	collect PID is 84008
	Waiting for receiver to become ready
	sizeof(Chunk): 1024
	sizeof(Packet): 1032
	sizeof(Event): 262664
	Listening for UDP packets
	Sending data. . . 
	Sent 5000 events in 2.73387 seconds
		(Data rate 460.799 MB/s)
	
	
	SIGINT received; shutting down gracefully
	
		320000 chunks received
		320000 chunks received
		320000 chunks received
		320000 chunks received
	Network input ended after for 2.74593 seconds
	Instructing disk output threads to stop after writing remaining 951 events
		Reaped 0 incomplete events
		2496 events written
			average batch size 27.7333
		2504 events written
			average batch size 28.1348
	Ran for 3.56997 seconds
	Successfully wrote 5000 events
	0 events were incomplete and not reaped
