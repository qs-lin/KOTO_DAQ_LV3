#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <limits>
#include <queue>
#include <thread>

#include <sys/stat.h>
#include <signal.h>


#include "event.h"
#include "udp_sockets.h"

std::atomic<bool> inputShutdownFlag, outputShutdownFlag;


struct PortInput{
    UDP::NetworkInputSocket inputSocket;

    void operator()();
};


void PortInput::operator()(){
    Chunk_32 my_data;
    EventRef nextBlank;

    size_t chunksReceived=0;
      while(true){
        if(inputShutdownFlag)
          break;


        ssize_t dataLen = 0;      
          dataLen=recvfrom(inputSocket.socket_fd, &my_data[0], sizeof(my_data),
          0,nullptr,0);

        if(dataLen==-1){
          int err=errno;
          if(err==EAGAIN)
            continue;
          if(err==EWOULDBLOCK) //will happen periodically due to SO_RCVTIMEO
            continue;
          perror("recvfrom() failed");
            throw std::runtime_error("recvfrom() failed");
           }
        if(dataLen<sizeof(PacketHeader)){
          throw std::runtime_error("Received packet too small to contain a valid header");
        }
        if(my_data[1]>=Event::nChunks_LV2){
          throw std::runtime_error("Received packet with invalid chunk index");
        }
        //if(dataLen=sizeof(my_data)){
        if(dataLen=sizeof(Chunk_32)){
          //std::cout << "chunk32" << sizeof(Chunk_32) << std::endl;
          //std::cout << "my_data" << sizeof(my_data) << std::endl;
          chunksReceived++;
        }
        
    }
    std::cout << '\t' << chunksReceived << " chunks received\n";
    //std::cout << '\t' << chunksReceived << " chunks received" << std::endl;

}

void shutDownHard(int sig){
  std::cerr << "\n\nSIGINT received again; shutting down hard\n\n";
  exit(1);
}

void shutDownGracefully(int sig){
  inputShutdownFlag=true;
  std::cerr << "\n\nSIGINT received; shutting down gracefully\n\n";
  signal(SIGINT, shutDownHard);
}


int main(int argc,char** argv){

  inputShutdownFlag=false;


  const int basePort=42000;
  size_t nListenPorts=0;
  const size_t nWriterThreads=2;

  if(argc>1)
    nListenPorts = std::stoul(argv[1]);

  if(nListenPorts<1)
    nListenPorts = 8;

  signal(SIGINT, shutDownGracefully);

  std::vector<std::thread> nits;
  for(unsigned int i=0; i<nListenPorts; i++){
    nits.emplace_back(PortInput{UDP::NetworkInputSocket(basePort+i)});
  }

  std::cout << "Listening for UDP packets" << std::endl;
      //for testing purposes, abuse creating a directoty as a way to communicate readiness
  mkdir(".recv_ready",01755);
  
  for(auto& nit : nits)
    nit.join();


  rmdir(".recv_ready");

  return 0;
}  



