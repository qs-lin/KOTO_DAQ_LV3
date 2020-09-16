#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>

#include "event.h"
#include "udp_sockets.h"

struct Sender{
	size_t nEvents;
	UDP::NetworkOutputSocket socket;
	size_t base;
	size_t skip;
	size_t totalChunk;
	
	void operator()(){
		//Packet packet;
	    Chunk_32 my_data;	
		for(uint32_t i=0;i<256;i++){
		  my_data[i] = i;
		}
		size_t eventsSent=0;
		while(eventsSent<nEvents){
			size_t spill=std::min(5000UL,nEvents-eventsSent);
			for(size_t i=0; i<spill; i++){
				std::chrono::high_resolution_clock::time_point nextEventTime
					=std::chrono::high_resolution_clock::now()
					+std::chrono::microseconds(500);
				//for(size_t j=base; j<Event::nChunks; j+=skip){
				for(size_t j=base; j<totalChunk; j+=skip){
					//packet.header.eventID=i;
					//packet.header.chunkIndex=j;
                    my_data[0] = i;
					my_data[1] = j;
					my_data[2] = 0;
                    if(j==totalChunk-1)
					  my_data[2] = j;
					int sendResult=sendto(socket.socket_fd, &my_data, sizeof(my_data), 0, socket.sock_addr->ai_addr, socket.sock_addr->ai_addrlen);
					if(sendResult==-1){ 
						perror("sendto() failed");
						exit(1);
					}
					else{
						//std::cout << " sent " << sendResult << " bytes" << std::endl;
					}
				}
				std::this_thread::sleep_until(nextEventTime);
			}
			eventsSent+=spill;
			if(eventsSent<nEvents) //simulate beam being off
				std::this_thread::sleep_for(std::chrono::seconds(2));
		}
	}
};

int main(int argc, char* argv[]){

	{ //wait for receiver to be running
		std::cout << "Waiting for receiver to become ready" << std::endl;
		struct stat buf;

		while(true){
			int result=stat(".recv_ready",&buf);
			if(result==0)
				break;
			if(result==-1 && errno!=ENOENT)
				perror("Unexpected stat() error");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

	}
	

	size_t nEvents=0;
	size_t nSenders=0;
	size_t chunkNumber=0;

	if(argc>3){
	  nEvents     = std::stoul(argv[1]);
      nSenders    = std::stoul(argv[2]);	
      chunkNumber = std::stoul(argv[3]);	
    };
		
	if(nEvents<1)
		nEvents=5000;

	if(nSenders<1)
		nSenders=8;

	if(chunkNumber<1)
		chunkNumber=256;
	
	int basePort=42000;
	std::vector<std::thread> senders;
	assert(Event::nChunks%nSenders==0);
	
	std::chrono::high_resolution_clock::time_point t0,t1;
	std::chrono::duration<double,std::ratio<1,1>> elapsed;
	t0=std::chrono::high_resolution_clock::now();

/*
//    using Chunk_32=std::array<uint32_t,256>;
    Chunk_32 packet;
    for(int i=0;i<256;i++){
	  packet[i] = i*100;
    }
    UDP::NetworkOutputSocket socket{"0.0.0.0",std::to_string(basePort)}; 
    int sendResult=sendto(socket.socket_fd, &packet, sizeof(packet), 0, socket.sock_addr->ai_addr, socket.sock_addr->ai_addrlen);
    if(sendResult==-1){
      perror("sendto() failed");
      exit(1);
    }
*/


	std::cout << "Sending data. . . " << std::endl;
    std::cout << "Chunk Number of each event :" << chunkNumber << std::endl;
	for(unsigned int i=0; i<nSenders; i++)
		//senders.emplace_back(Sender{nEvents,UDP::NetworkOutputSocket{"0.0.0.0",std::to_string(basePort+i)},i,nSenders});
		senders.emplace_back(Sender{nEvents,UDP::NetworkOutputSocket{"192.168.110.15",std::to_string(basePort+i)},i,nSenders,chunkNumber});





//  move the following 3 lines of codes upwards by Qisen	
/*
	std::chrono::high_resolution_clock::time_point t0,t1;
	std::chrono::duration<double,std::ratio<1,1>> elapsed;
	t0=std::chrono::high_resolution_clock::now();
*/	

	for(auto& sender : senders)
		sender.join();

	t1=std::chrono::high_resolution_clock::now();
	elapsed=t1-t0;
	std::cout << "Sent " << nEvents << " events in " << elapsed.count() << " seconds" << std::endl;
	std::cout << "\t(Data rate " << (nEvents*Event::nChunks*sizeof(Packet)/elapsed.count())/(1UL<<20) << " MB/s)" << std::endl; 

	return 0;
}
