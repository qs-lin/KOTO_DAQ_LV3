CXXFLAGS=-std=c++11 -O3 -g
LDFLAGS=-pthread

all : generate collect

collect : makefile collect.cpp event.h udp_sockets.h
	$(CXX) $(CXXFLAGS) collect.cpp $(LDFLAGS) -o collect

generate : makefile generate.cpp event.h udp_sockets.h
	$(CXX) $(CXXFLAGS) generate.cpp $(LDFLAGS) -o generate

clean : 
	rm -rf generate collect generate.dSYM collect.dSYM output_events_*
