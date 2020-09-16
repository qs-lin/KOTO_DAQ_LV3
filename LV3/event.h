#ifndef EVENT_H
#define EVENT_H

#include <array>
#include <mutex>
#include <atomic>
#include <cstring>
#include <functional>
#define MAXLINE 9000
///Borrowed from libcuckoo:
///A fast, lightweight spinlock
///If many locks are stored close together and used frequently, over-alignment
///may be advantageous to prevent false sharing issues. 

using Chunk_32=std::array<uint32_t,256>;
    //unsigned char my_data[MAXLINE];
using Chunk_LV2 = std::array<unsigned char, MAXLINE>; 

class /*alignas(64)*/ spinlock {
public:
	spinlock(){ lock_.clear(); }
	
	spinlock(const spinlock &other)=delete;
	
	spinlock& operator=(const spinlock &other)=delete;
	
	void lock() noexcept {
		while (lock_.test_and_set(std::memory_order_acq_rel))
			;
	}
	
	void unlock() noexcept { lock_.clear(std::memory_order_release); }
	
	bool try_lock() noexcept {
		return !lock_.test_and_set(std::memory_order_acq_rel);
	}
private:
	std::atomic_flag lock_;
};

///A portion of an event, small enough to fit within an ethernet frame
using Chunk=std::array<uint32_t,256>;
//using Chunk=std::array<uint8_t,2048>;
static_assert(sizeof(Chunk)==1024,"Event chunks must have no padding");
//static_assert(sizeof(Chunk)==2048,"Event chunks must have no padding");


using EventID=uint32_t;
using ChunkCountType=uint16_t;

struct Event{
	///the total number of chunks which make up an event
	//static constexpr ChunkCountType nChunks=255;
	//static constexpr ChunkCountType nChunks=256;
	static constexpr ChunkCountType nChunks_LV2=10;
	///the event's data payload
	//std::array<Chunk_32,nChunks> data;
	std::array<Chunk_LV2,nChunks_LV2> data_LV2;
	//static_assert(sizeof(data)==nChunks*sizeof(Chunk_32),"Event data must be contiguous");
	std::array<spinlock,nChunks_LV2> chunkLocks;
	///bitmaps of data chunks which are available
	std::array<std::atomic<bool>,nChunks_LV2> chunksReady;
    std::atomic<bool> receiveFooterFlag;
    //std::atomic<bool> in_use;
    std::atomic<uint32_t> nChunks_new; 
    //bool receiveFooterFlag = false;
    //uint32_t nChunks_new = 0;

    std::atomic<unsigned int> crateid;
    std::atomic<unsigned int> spillid;
	spinlock transferLock;
    //std::mutex transferLock;	
	EventID id;


	
	Event(){}
	
    
	bool isComplete() const{
		//On the theory that chunks should mostly arrive in order, check from
		//last to first to bail out as early as possible in the common case 
		//that a chunk is not ready and hopefully avoid contention with threads
		//doing the useful work of filling in chunks. 
		for(ChunkCountType i=nChunks_LV2-1; i>0; i--){
			if(!chunksReady[i])
				return false;
		}
		return chunksReady[0];
	}
    

    bool isComplete(uint32_t totalChunk) const{

      for(ChunkCountType i=totalChunk; i>0; i--){
        if(!chunksReady[i])
          return false;
      }
      return chunksReady[0];
    }

	
	void reset(){
		for(ChunkCountType i=0; i!=nChunks_LV2; i++)
			chunksReady[i]=false;
        receiveFooterFlag = false;
        nChunks_new = 0;
	}
};

using EventRef=Event*;

struct PacketHeader{
	EventID eventID;
	ChunkCountType chunkIndex;
	uint16_t reserved;
};

struct Packet{
	PacketHeader header;
	Chunk data;
	
	//Packet(){ memset(&data, 0, sizeof(data)); }
    //modified by Qisen. 97 is human readable
	Packet(){ memset(&data, 97, sizeof(data)); }
};

static_assert(sizeof(Packet)==sizeof(PacketHeader)+sizeof(Chunk),"Event data must be contiguous");

#endif //EVENT_H
