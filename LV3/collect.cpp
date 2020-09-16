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
#include <mutex> 


//std::mutex mtx;
bool forceShutdownFlag = false;
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
//const std::chrono::milliseconds	incompleteEventGracePeriod(1000);
const std::chrono::milliseconds	incompleteEventGracePeriod(1500);
struct EventExpiry{
	EventID id;
	std::chrono::steady_clock::time_point time;
};
///Queue of records for events which have begun loading, but may not complete
moodycamel::BlockingConcurrentQueue<EventExpiry> EventTimeouts;

std::mutex summaryLock;
//# of last chunk received
std::atomic<size_t> globalEventsReceived;
//# of events tranferred into ReadyToWrite
std::atomic<size_t> globalEventsWritten;
//# of events extracted from ReadyToWrite
std::atomic<size_t> globalEventsSaved;
//# of duplicated packets
std::atomic<size_t> duplicatedPackets;
//# of packets informed by LV2 
std::atomic<size_t> lv2Packets;

struct NetworkInput{
	moodycamel::ConsumerToken blankConsumeToken;
	moodycamel::ProducerToken writeProduceToken;
	UDP::NetworkInputSocket inputSocket;
	
	void operator()();
};

void NetworkInput::operator()(){
	Packet packet;
	//Chunk_32 my_data;
    unsigned char my_data[MAXLINE];
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
	//size_t chunksReceived_debug=0;
    std::array<unsigned int,3> header;

	while(true){
		if(inputShutdownFlag)
			break;
		//obtain a data chunk
		ssize_t dataLen;
		{ //attempt to get a packet
			dataLen=recvfrom(inputSocket.socket_fd, &my_data[0], sizeof(my_data), 
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
		if(my_data[1]>=Event::nChunks_LV2){
			throw std::runtime_error("Received packet with invalid chunk index");
		}
//		std::cout << "\tGot chunk " << packet.header.chunkIndex << " of event " << packet.header.eventID << std::endl;
		
		//find the event to which the chunk belongs
		EventRef targetEvent=nullptr;
        for(int i=0;i<3;i=i+1){
          header[i] = (unsigned int)my_data[4*i]*16777216 + (unsigned int)my_data[4*i+1]*65536 + (unsigned int)my_data[4*i+2]*256 + (unsigned int)my_data[4*i+3];
          //header[i] =  (decoder[4*i]<<24) |  (decoder[4*i+1]<<16) |  (decoder[4*i+2]<<8) |  decoder[4*i+3];
        }
        //chunksReceived_debug++;
        unsigned int lv2_info = ((header[2] & 0xFFFE0000) >> 17);
        //unsigned int lv2_info = (header[2] & 0xFFFE0000); 
        if(lv2_info != 0 ){
          inputShutdownFlag = true;
          lv2Packets.fetch_add(lv2_info);
          std::cout << "receive lv2 information" << std::endl;
          break;
        }    

        /*
        if(globalEventsWritten.load()==16000){
          inputShutdownFlag = true;        
          std::cout <<"receive !! " << std::endl;
        }
        */

		//Lots going on here. We use upsert to try to insert our on-hand blank
		//event into the Incoming Event Map as the event for the chunk's target
		//ID. If the ID is already in the map, no insertion will be performed, 
		//and our callback will be invoked on the existing mapped value, storing
		//it into targetEvent. If it is not found, the fresh blank will be 
		//inserted under the ID, and inserted will be true, indicating that we 
		//must treat the blank as the target, and obtain a new blank. 
		bool inserted=IncomingEvents.upsert(header[0],
		                                    [&targetEvent](const EventRef& evt){
		                                    	targetEvent=evt;
		                                    },nextBlank);
        //targetEvent->reset();
		if(inserted){
//			std::cout << "\tAdded event to IEM" << std::endl;
			//our ready blank is now the target event
			targetEvent=nextBlank;
            //targetEvent->reset();
			//since it was blank we must set the ID
			//targetEvent->id=packet.header.eventID;
			targetEvent->id=header[0];
			//finally, obtain a new blank
			getNextBlank();
			//inform the Incomplete Event Reaper that it should track the new event
			EventTimeouts.enqueue(EventExpiry{targetEvent->id,
				std::chrono::steady_clock::now()+incompleteEventGracePeriod
			});
		}
		
		{//insert this chunk into the event
			//hold the chunk's corresponding lock while copying its data
			std::lock_guard<spinlock> lock(targetEvent->chunkLocks[header[1]]);
			//if(targetEvent->chunksReady[packet.header.chunkIndex]){
			if(targetEvent->chunksReady[header[1]]){
				//if the chunk is already here, we must have a duplicate packet,
				//so we throw it away to avoid unnecessary work and the 
				//possibility of making a mess in case the event is complete and 
				//another thread is trying to hand it off for processing.
//				std::cout << "\tChunk already present, ignoring" << std::endl;
				continue;
			}
			//memcpy(&targetEvent->data[packet.header.chunkIndex],&packet.data,sizeof(Chunk));
			//memcpy(&targetEvent->data[my_data[1]],&my_data[0],sizeof(Chunk_32));
			memcpy(&targetEvent->data_LV2[header[1]],&my_data[0],dataLen);
//			std::cout << "\tCopied chunk data" << std::endl;
			targetEvent->chunksReady[header[1]]=true;
			chunksReceived++;
		}
		//Finally, if the all of the event's chunks have arrived, move it to the
		//Ready To Write Queue. 
        if(header[2]!=0){
          targetEvent->receiveFooterFlag = true;
          targetEvent->nChunks_new = header[1];
          targetEvent->crateid = (unsigned int)my_data[11]; 
          targetEvent->spillid = (unsigned int)my_data[10]+(unsigned int)my_data[9]*256; 
          /*
          std::unique_lock<spinlock> lock(targetEvent->transferLock,std::try_to_lock);
          if(lock.owns_lock())
            lock_ = true;
          */
          //lock2 = lock(targetEvent->transferLock,std::try_to_lock);
          //chunksLast++;
          //lock2 = (std::unique_lock<spinlock>) lock(targetEvent->transferLock,std::try_to_lock);
          //std::cout << targetEvent->nChunks_new << std::endl;
		  globalEventsReceived.fetch_add(1);
        }  


		//if(targetEvent->nChunks_new>0 && targetEvent->isComplete(targetEvent->nChunks_new)) { 
		if(targetEvent->receiveFooterFlag && targetEvent->isComplete(targetEvent->nChunks_new)){ 
	    //if(targetEvent->isComplete()){	
        //std::cout << "\tEvent is complete" << std::endl;
			//Ensure that two threads do not both try to do the hand-off 
			//std::unique_lock<spinlock> lock(targetEvent->transferLock,std::try_to_lock);
            //{std::lock_guard<spinlock> lock(targetEvent->transferLock);
			std::unique_lock<spinlock> lock(targetEvent->transferLock,std::try_to_lock);
            //std::mutex mtx = targetEvent->transferLock;
			//std::unique_lock<std::mutex> lock(targetEvent->transferLock,std::try_to_lock);
			//std::unique_lock<std::mutex> lock(mtx,std::try_to_lock);
			//std::unique_lock<std::mutex> lock(mtx,std::try_to_lock);
			if(lock.owns_lock()){ //this thread has acquired sole responsibility for the hand-off
           // if( (targetEvent->in_use)==false ){
            
                //std::cout << "\tHanding off event for processing" << std::endl;
                
              //targetEvent->in_use = true;
		      IncomingEvents.erase(targetEvent->id);
		      eventsToWrite.fetch_add(1);
		      globalEventsWritten.fetch_add(1);
		      ReadyToWrite.enqueue(/*writeProduceToken,*/targetEvent);
            }
		}
	}//end of while
	{
		std::lock_guard<std::mutex> lock(summaryLock);
		std::cout << '\t' << chunksReceived << " chunks received\n";
		//std::cout << '\t' << chunksReceived_debug << " chunks_debug received\n";
		//std::cout << '\t' << chunksLast<< " footer received\n";
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
//			std::cout << __PRETTY_FUNCTION__ << ", thread " << std::this_thread::get_id() << " waiting for events to write" << std::endl;
			std::size_t count=ReadyToWrite.wait_dequeue_bulk_timed(/*writeConsumeToken,*/eventBuffer.data(),nWriteBufferEvents,std::chrono::milliseconds(10));
			if(count==0){
				if(eventsToWrite.load()==0 && outputShutdownFlag){
					break;
				}
				continue;
			}
//			std::cout << "\tGot " << count << " events to write" << std::endl;
			//eventsToWrite.fetch_sub(count);
			for(size_t i=0; i<count; i++){
			  EventRef event=eventBuffer[i];

              /*
                std::string debugfile = "debug.txt";
                std::ofstream dfile(debugfile.c_str(),std::ofstream::app);
                dfile << EventId << std::endl;
                dfile.close();
              */
   				int EventId = event->id;
                int fileid = 0;
   				int CrateId = event->crateid;
   				int SpillId = event->spillid;
  			    //std::string outputfile = "/local/s1/toyDAQ_0217/myfile_spill_";
  			    std::string outputfile = "/local/s1/data/spill";
                //std::cout << "spill id:" << SpillId << std::endl;
 				outputfile += std::to_string(SpillId);
                mkdir(outputfile.c_str(),01755);
 				outputfile += "/evt_"; 
                fileid = (EventId%2==0) ? (EventId/2) : (EventId/2) + 1;
 				outputfile += std::to_string(fileid);
 				outputfile += "_crate_"; 
 				outputfile += std::to_string(CrateId);
			    std::ofstream ofile(outputfile.c_str());
				//std::array<Chunk_32,Event::nChunks> data = event->data;
			    ofile.write((const char*)&event->id,sizeof(event->id));
			    ofile.write((const char*)&event->data_LV2,sizeof(event->data_LV2));
				
				outfile.close();
				std::this_thread::sleep_for(std::chrono::microseconds(1000));
				event->reset();
			}

			eventsToWrite.fetch_sub(count);
			BlankEvents.enqueue_bulk(/*blankProduceToken,*/eventBuffer.data(),count);
//			std::cout << "\tEstimated size of BEP: " << BlankEvents.size_approx() << std::endl;
			eventsWritten+=count;
			batches++;
		}
		{
			std::lock_guard<std::mutex> lock(summaryLock);
			globalEventsSaved.fetch_add(eventsWritten);
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
	outputShutdownFlag=true;
    forceShutdownFlag=true;
	std::cerr << "\n\nSIGINT received; shutting down gracefully\n\n";
	signal(SIGINT, shutDownHard);
}

int main(int argc,char** argv){
  std::chrono::high_resolution_clock::time_point t_s, t_e;
  std::chrono::duration<double,std::ratio<1,1>> elapsed_v1;
  t_s=std::chrono::high_resolution_clock::now();

  /*
  std::cout << "sizeof(Chunk): " << sizeof(Chunk_32) << std::endl;
  std::cout << "sizeof(Header): " << sizeof(PacketHeader) << std::endl;
  std::cout << "sizeof(Packet): " << sizeof(Packet) << std::endl;
  std::cout << "sizeof(Event): " << sizeof(Event) << std::endl;
  */
  inputShutdownFlag=false;
  outputShutdownFlag=false;
  globalEventsWritten=0;
  globalEventsReceived=0;
  globalEventsSaved=0;
  forceShutdownFlag=false;
	
  const int basePort=42000;
  size_t nListenPorts=8;
  size_t nWriterThreads=2;

  if(argc>2){
    nListenPorts   = std::stoul(argv[1]);
    nWriterThreads = std::stoul(argv[2]);;
  }
  if(nListenPorts<1){
    nListenPorts   = 8;
    nWriterThreads = 2;
  }
  //preallocate data buffers
  //this should be at least the number of events per spill this receiver is 
  //expected to handle per data-taking period
  //preallocatedEvents.resize(8192);
  //preallocatedEvents.resize(100);
  preallocatedEvents.resize(30000);
  //preallocatedEvents.resize(3000);
  //preallocatedEvents.resize(80000);
  //preallocatedEvents.resize(1000000);
  for(auto& event : preallocatedEvents)
    BlankEvents.enqueue(&event);
	
    //std::cout << "size of Blank events:" << BlankEvents.BLOCK_SIZE << std::endl;
  signal(SIGINT, shutDownGracefully);

  std::chrono::high_resolution_clock::time_point t0,t1,t2;
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
  t_e=std::chrono::high_resolution_clock::now();
  elapsed_v1=t_e-t_s;
  std::cout << "it takes: " << elapsed_v1.count() << " seconds to wake up listening ports" << std::endl;

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
	
  //t1=std::chrono::high_resolution_clock::now();
  t2=std::chrono::high_resolution_clock::now();
  elapsed=t2-t1;
  //std::cout << "Ran for " << elapsed.count() << " seconds" << std::endl;
  std::cout << "Ran for " << elapsed.count() << " seconds to write down files " << std::endl;
  std::cout << "LV2 has sent out:      " << lv2Packets           << " events" << std::endl;
  std::cout << "Successfully received: " << globalEventsReceived << " events" << std::endl;
  std::cout << "Successfully wrote:    " << globalEventsWritten  << " events" << std::endl;
  std::cout << "Successfully saved:    " << globalEventsSaved    << " events" << std::endl;
  std::cout << "Duplicated packet:     " << duplicatedPackets                 << std::endl; 
  //std::cout << ReadyToWrite.BLOCK_SIZE  << " events were to write" << std::endl;

  std::string name = "history.txt";
  std::ofstream ofile;
  ofile.open(name.c_str(),std::ofstream::app);

  ofile << "Ran for " << elapsed.count() << " seconds to write down files " << std::endl;
  ofile << "LV2 has sent out:      " << lv2Packets           << " events" << std::endl;
  ofile << "Successfully received: " << globalEventsReceived << " events" << std::endl;
  ofile << "Successfully wrote:    " << globalEventsWritten  << " events" << std::endl;
  ofile << "Successfully saved:    " << globalEventsSaved    << " events" << std::endl;
  ofile << "Duplicated packet:     " << duplicatedPackets                 << std::endl;
  ofile.close();





    /*
    t1=std::chrono::high_resolution_clock::now();
    preallocatedEvents.clear();
    t2=std::chrono::high_resolution_clock::now();
	elapsed=t2-t1;
	std::cout << "Ran for " << elapsed.count() << " seconds to clear" << std::endl;
    */


    if(forceShutdownFlag==false){
      std::cout << "exiting current program, automatically starting the new one" << std::endl; 
      std::system("./collect 1 8 &");
    }else{
      std::cout << "killed by users. program ends" << std::endl;    
    }  
	rmdir(".recv_ready");

	return 0;
}
