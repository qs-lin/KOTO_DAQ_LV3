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
	
  void operator()(){
    Packet packet;
    size_t eventsSent=0;
    while(eventsSent<nEvents){
      size_t spill=std::min(5000UL,nEvents-eventsSent);
        for(size_t i=0; i<spill; i++){
        std::chrono::high_resolution_clock::time_point nextEventTime
        =std::chrono::high_resolution_clock::now()
        +std::chrono::microseconds(500);
        for(size_t j=base; j<Event::nChunks; j+=skip){
          packet.header.eventID=i;
          packet.header.chunkIndex=j;
          int sendResult=sendto(socket.socket_fd, &packet, sizeof(packet), 0, socket.sock_addr->ai_addr, socket.sock_addr->ai_addrlen);
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
  if(argc>2){
    nEvents=std::stoul(argv[1]);
    nSenders=std::stoul(argv[2]);
  }
  if(nEvents<1)
    nEvents=5000;
  if(nSenders<1)
    nSenders=4;
	
  int basePort=42000;
  std::vector<std::thread> senders;
	//size_t nSenders=4;
  assert(Event::nChunks%nSenders==0);
	
  std::cout << "Sending data. . . " << std::endl;
  for(unsigned int i=0; i<nSenders; i++)
    senders.emplace_back(Sender{nEvents,UDP::NetworkOutputSocket{"0.0.0.0",std::to_string(basePort+i)},i,nSenders});
	
  std::chrono::high_resolution_clock::time_point t0,t1;
  std::chrono::duration<double,std::ratio<1,1>> elapsed;
  t0=std::chrono::high_resolution_clock::now();
	
  for(auto& sender : senders)
    sender.join();

  t1=std::chrono::high_resolution_clock::now();
  elapsed=t1-t0;
  std::cout << "Sent " << nEvents << " events in " << elapsed.count() << " seconds" << std::endl;
  std::cout << "\t(Data rate " << (nEvents*Event::nChunks*sizeof(Packet)/elapsed.count())/(1UL<<20) << " MB/s)" << std::endl; 
	
  return 0;
}
