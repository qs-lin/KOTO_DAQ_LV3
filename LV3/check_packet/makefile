CXXFLAGS=-std=c++11 -O3 -g
LDFLAGS=-pthread

all : check check_vary 

check: makefile check.cpp event.h 
	$(CXX) $(CXXFLAGS) check.cpp $(LDFLAGS) -o check 

check_vary: makefile check_vary.cpp event.h 
	$(CXX) $(CXXFLAGS) check_vary.cpp $(LDFLAGS) -o check_vary 


clean : 
	rm -rf check_nic test read generate collect check debug debug.dSYM generate.dSYM collect.dSYM read.dSYM check_nic.dSYM test.dSYM check.dSYM output_events_*
