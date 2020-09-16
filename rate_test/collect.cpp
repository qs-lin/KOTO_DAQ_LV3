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

#include "blockingconcurrentqueue.h"
#include "libcuckoo/cuckoohash_map.hh"

#include "event.h"
#include "udp_sockets.h"

std::atomic<bool> inputShutdownFlag, outputShutdownFlag;
///Backing storage for events, to avoid allocations while running in steady state
std::deque<Event> preallocatedEvents;
///Events which are in the process of being read
libcuckoo::cuckoohash_map<EventID,EventRef> IncomingEvents;
///Empty event objects ready for reuse
moodycamel::BlockingConcurrentQueue<EventRef> BlankEvents;
///Number of events currently in the ReadyToWrite queue.
///This is useful for ensuring clean shutdown. 
std::atomic<size_t> eventsToWrite(0);
///Complete events which should now be written to disk
moodycamel::BlockingConcurrentQueue<EventRef> ReadyToWrite;
///Complete events which should now be transmitted for processing.
///Currently unused
moodycamel::BlockingConcurrentQueue<EventRef> ReadyToTransmit;

///This is the maximum amount of time allowed for all packets making up an event 
///to arrive before the event will be discarded. Setting this too low will cause
///valid events to be lost as their constituant packets arrive over more than 
///one period. However, there is no particular harm in allowing this period to
///be realtively large, up to the scale of the spill period, since its only
///purpose is to ensure that the NITs are not starved of buffers for incoming 
///data due to events with missing packets taking up buffers forever. 
const std::chrono::milliseconds	incompleteEventGracePeriod(1000);
struct EventExpiry{
  EventID id;
  std::chrono::steady_clock::time_point time;
};
///Queue of records for events which have begun loading, but may not complete
moodycamel::BlockingConcurrentQueue<EventExpiry> EventTimeouts;

std::mutex summaryLock;
std::atomic<size_t> globalEventsWritten;

struct NetworkInput{
  moodycamel::ConsumerToken blankConsumeToken;
  moodycamel::ProducerToken writeProduceToken;
  UDP::NetworkInputSocket inputSocket;
	
  void operator()();
};

void NetworkInput::operator()(){
  Packet packet;
  EventRef nextBlank;
	
  auto getNextBlank=[this,&nextBlank]{
  bool got=BlankEvents.try_dequeue(/*blankConsumeToken,*/nextBlank);
  //TODO: THis should be refactored to block at the cost of losing some 
  //data rather than taking down data taking permanently. Some 
  //low-overhead feedback mechanism will be needed to provide alerts when
  //this happens, however. 
  if(!got)
    throw std::runtime_error("Blank Event Pool underflow");
  assert(nextBlank);
  };
//hold one blank event ready to use at all times
  getNextBlank();
	
  size_t chunksReceived=0;
  while(true){
    if(inputShutdownFlag)
      break;
		//obtain a data chunk
    ssize_t dataLen;
    { //attempt to get a packet
      dataLen=recvfrom(inputSocket.socket_fd, &packet, sizeof(packet), 
      /*flags*/ 0, 
      /*don't care about source adddress*/ nullptr, 0);
    }
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
    if(packet.header.chunkIndex>=Event::nChunks){
      throw std::runtime_error("Received packet with invalid chunk index");
    }
    //std::cout << "\tGot chunk " << packet.header.chunkIndex << " of event " << packet.header.eventID << std::endl;
		
    //find the event to which the chunk belongs
    EventRef targetEvent=nullptr;
    //Lots going on here. We use upsert to try to insert our on-hand blank
    //event into the Incoming Event Map as the event for the chunk's target
    //ID. If the ID is already in the map, no insertion will be performed, 
    //and our callback will be invoked on the existing mapped value, storing
    //it into targetEvent. If it is not found, the fresh blank will be 
    //inserted under the ID, and inserted will be true, indicating that we 
    //must treat the blank as the target, and obtain a new blank. 
    bool inserted=IncomingEvents.upsert(packet.header.eventID,
    [&targetEvent](const EventRef& evt){
    targetEvent=evt;
    },nextBlank);
    if(inserted){
      //std::cout << "\tAdded event to IEM" << std::endl;
      //our ready blank is now the target event
      targetEvent=nextBlank;
      //since it was blank we must set the ID
      targetEvent->id=packet.header.eventID;
      //finally, obtain a new blank
      getNextBlank();
      //inform the Incomplete Event Reaper that it should track the new event
      EventTimeouts.enqueue(EventExpiry{targetEvent->id,
      std::chrono::steady_clock::now()+incompleteEventGracePeriod
      });
    }
		
    {//insert this chunk into the event
    //hold the chunk's corresponding lock while copying its data
      std::lock_guard<spinlock> lock(targetEvent->chunkLocks[packet.header.chunkIndex]);
      if(targetEvent->chunksReady[packet.header.chunkIndex]){
      //if the chunk is already here, we must have a duplicate packet,
      //so we throw it away to avoid unnecessary work and the 
      //possibility of making a mess in case the event is complete and 
      //another thread is trying to hand it off for processing.
      //std::cout << "\tChunk already present, ignoring" << std::endl;
        continue;
      }
      memcpy(&targetEvent->data[packet.header.chunkIndex],&packet.data,sizeof(Chunk));
      //std::cout << "\tCopied chunk data" << std::endl;
      targetEvent->chunksReady[packet.header.chunkIndex]=true;
      chunksReceived++;
    }
		
    //Finally, if the all of the event's chunks have arrived, move it to the
    //Ready To Write Queue. 
    if(targetEvent->isComplete()){
      //std::cout << "\tEvent is complete" << std::endl;
      //Ensure that two threads do not both try to do the hand-off 
      std::unique_lock<spinlock> lock(targetEvent->transferLock,std::try_to_lock);
      if(lock.owns_lock()){ //this thread has acquired sole responsibility for the hand-off
      //std::cout << "\tHanding off event for processing" << std::endl;
        IncomingEvents.erase(targetEvent->id);
        eventsToWrite.fetch_add(1);
        ReadyToWrite.enqueue(/*writeProduceToken,*/targetEvent);
      }
    }
  }
  {
    std::lock_guard<std::mutex> lock(summaryLock);
    std::cout << '\t' << chunksReceived << " chunks received\n";
  }
}

struct DiskOutput{
  //moodycamel::ConsumerToken writeConsumeToken;
  //moodycamel::ProducerToken blankProduceToken;
  std::string outfileName;
	
  void operator()(){
  constexpr size_t nWriteBufferEvents=32;
  std::array<EventRef,nWriteBufferEvents> eventBuffer;
  std::ofstream outfile(outfileName);

  size_t eventsWritten=0, batches=0;
  while(true){
  //std::cout << __PRETTY_FUNCTION__ << ", thread " << std::this_thread::get_id() << " waiting for events to write" << std::endl;
    std::size_t count=ReadyToWrite.wait_dequeue_bulk_timed(/*writeConsumeToken,*/eventBuffer.data(),nWriteBufferEvents,std::chrono::milliseconds(10));
    if(count==0){
      if(eventsToWrite.load()==0 && outputShutdownFlag){
        break;
      }
      continue;
    }
    //std::cout << "\tGot " << count << " events to write" << std::endl;
    eventsToWrite.fetch_sub(count);
    for(size_t i=0; i<count; i++){
      EventRef event=eventBuffer[i];
				
      //really write
      outfile.write((const char*)&event->id,sizeof(event->id));
      outfile.write((const char*)&event->data,sizeof(event->data));
				
      //simulate writing at a speed of ~200 MB/s
      //std::this_thread::sleep_for(std::chrono::microseconds(1250));
				
      event->reset();
    }
    BlankEvents.enqueue_bulk(/*blankProduceToken,*/eventBuffer.data(),count);
    //std::cout << "\tEstimated size of BEP: " << BlankEvents.size_approx() << std::endl;
    eventsWritten+=count;
    batches++;
  }
  {
    std::lock_guard<std::mutex> lock(summaryLock);
    globalEventsWritten.fetch_add(eventsWritten);
    std::cout << '\t' << eventsWritten << " events written\n";
    std::cout << "\t\taverage batch size " << double(eventsWritten)/batches << '\n';
  }
  }
};

struct IncompleteEventReaper{
  struct ExpirationOrder{
    bool operator()(const EventExpiry& e1, const EventExpiry& e2){
      return e2.time<e1.time;
    }
  };
	
  size_t eventsReaped;
   std::priority_queue<EventExpiry,std::deque<EventExpiry>,ExpirationOrder> expiring;
	
  IncompleteEventReaper():
  eventsReaped(0){}
	
  void operator()(){
    std::vector<EventExpiry> buffer(32);
    decltype(incompleteEventGracePeriod) sleepTime(std::min((decltype(incompleteEventGracePeriod)::rep)1,incompleteEventGracePeriod.count()/10));
    while(true){
      std::this_thread::sleep_for(sleepTime);
      //grab some expiration records from the input queue
      std::size_t count=0;
      do{
        count=EventTimeouts.try_dequeue_bulk(buffer.data(),buffer.size());
        for(size_t i=0; i<count; i++)
          expiring.push(buffer[i]);
      }while(count!=0);
      //process all records whose expiration times have passed
      auto now=std::chrono::steady_clock::now();
      while(!expiring.empty() && expiring.top().time<now){
        auto record=expiring.top();
        //if the event is still listed in the incoming table, remove it 
        //and grab the actual pointer to it
        EventRef event=nullptr;
        IncomingEvents.erase_fn(record.id,[&](const EventRef e){
          event=e;
          return true; //do erase
        });
				//if the event had to be removed reset it and return it to the BEP
        if(event){
          eventsReaped++;
          event->reset();
          BlankEvents.enqueue(event);
        }
        expiring.pop();
      }
      //check whether to stop
      if(outputShutdownFlag)
        break;
    }
    {
      std::lock_guard<std::mutex> lock(summaryLock);
      std::cout << "\tReaped " << eventsReaped << " incomplete events\n";
    }
  }
};

void shutDownHard(int sig){
  std::cerr << "\n\nSIGINT received again; shutting down hard\n\n";
  exit(1);
}

void shutDownGracefully(int sig){
  inputShutdownFlag=true;
  std::cerr << "\n\nSIGINT received; shutting down gracefully\n\n";
  signal(SIGINT, shutDownHard);
}

int main(int argc, char* argv[]){

  std::cout << "sizeof(Chunk): " << sizeof(Chunk) << std::endl;
  std::cout << "sizeof(Packet): " << sizeof(Packet) << std::endl;
  std::cout << "sizeof(Event): " << sizeof(Event) << std::endl;
	
  inputShutdownFlag=false;
  outputShutdownFlag=false;
  globalEventsWritten=0;
	
  const int basePort=42000;

  size_t nListenPorts=0;
  size_t nWriterThreads=0;

if(argc>2){
  nListenPorts=std::stoul(argv[1]);
  nWriterThreads=std::stoul(argv[2]);
}

if(nListenPorts<1)
  nListenPorts=4;
if(nWriterThreads<1)
  nWriterThreads=2;

  //const size_t nListenPorts=4;
  //const size_t nWriterThreads=2;
	
  //preallocate data buffers
  //this should be at least the number of events per spill this receiver is 
  //expected to handle per data-taking period
  preallocatedEvents.resize(8192);
  for(auto& event : preallocatedEvents)
    BlankEvents.enqueue(&event);
	
  signal(SIGINT, shutDownGracefully);
	
  std::chrono::high_resolution_clock::time_point t0,t1;
  std::chrono::duration<double,std::ratio<1,1>> elapsed;
  t0=std::chrono::high_resolution_clock::now();
	
  std::vector<std::thread> nits;
  for(unsigned int i=0; i<nListenPorts; i++){
    nits.emplace_back(NetworkInput{moodycamel::ConsumerToken(BlankEvents),
    moodycamel::ProducerToken(ReadyToWrite),
    UDP::NetworkInputSocket(basePort+i)});
  }
  std::thread iert(IncompleteEventReaper{});
	
  std::cout << "Listening for UDP packets" << std::endl;
  //for testing purposes, abuse creating a directoty as a way to communicate readiness
  mkdir(".recv_ready",01755);
	
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
	
  std::vector<std::thread> dots;
  for(unsigned int i=0; i<nWriterThreads; i++){
    dots.emplace_back(DiskOutput{//moodycamel::ConsumerToken(ReadyToWrite),
    //moodycamel::ProducerToken(BlankEvents),
    "output_events_"+std::to_string(i)
    });
  }
	
  for(auto& nit : nits)
    nit.join();
  //only instruct the output thread(s) to shut down after the input threads have stopped
  t1=std::chrono::high_resolution_clock::now();
  elapsed=t1-t0;
  std::cout << "Network input ended after for " << elapsed.count() << " seconds" << std::endl;
  std::cout << "Instructing disk output threads to stop after writing remaining " << eventsToWrite << " events" << std::endl;
  outputShutdownFlag=true;
  iert.join();
  for(auto& dot : dots)
    dot.join();
	
  t1=std::chrono::high_resolution_clock::now();
  elapsed=t1-t0;
  std::cout << "Ran for " << elapsed.count() << " seconds" << std::endl;
  std::cout << "Successfully wrote " << globalEventsWritten << " events" << std::endl;
  std::cout << IncomingEvents.size() << " events were incomplete and not reaped" << std::endl;
	
  rmdir(".recv_ready");
return 0;
}
