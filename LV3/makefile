CXXFLAGS=-std=c++11 -O3 -g
LDFLAGS=-pthread

all : collect read check_nic test check debug

collect : makefile collect.cpp event.h udp_sockets.h
	$(CXX) $(CXXFLAGS) collect.cpp $(LDFLAGS) -o collect

generate : makefile generate.cpp event.h udp_sockets.h
	$(CXX) $(CXXFLAGS) generate.cpp $(LDFLAGS) -o generate

read: makefile read.cpp event.h udp_sockets.h
	$(CXX) $(CXXFLAGS) read.cpp $(LDFLAGS) -o read 

check: makefile check.cpp event.h udp_sockets.h
	$(CXX) $(CXXFLAGS) check.cpp $(LDFLAGS) -o check 

check_nic: makefile check_nic.cpp event.h udp_sockets.h
	$(CXX) $(CXXFLAGS) check_nic.cpp $(LDFLAGS) -o check_nic 

test: makefile test.cpp event.h udp_sockets.h
	$(CXX) $(CXXFLAGS) test.cpp $(LDFLAGS) -o test 

debug: makefile debug.cpp 
	$(CXX) $(CXXFLAGS) debug.cpp $(LDFLAGS) -o debug 

clean : 
	rm -rf check_nic test read generate collect check debug debug.dSYM generate.dSYM collect.dSYM read.dSYM check_nic.dSYM test.dSYM check.dSYM output_events_*
