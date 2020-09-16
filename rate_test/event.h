#ifndef EVENT_H
#define EVENT_H

#include <array>
#include <atomic>
#include <cstring>
#include <functional>

///Borrowed from libcuckoo:
///A fast, lightweight spinlock
///If many locks are stored close together and used frequently, over-alignment
///may be advantageous to prevent false sharing issues. 
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
//using Chunk=std::array<uint8_t,1024>;
//const int nbyte = 1320;
const int nbyte = 1440;
using Chunk=std::array<uint8_t,nbyte>;
static_assert(sizeof(Chunk)==nbyte,"Event chunks must have no padding");

using EventID=uint32_t;
using ChunkCountType=uint16_t;

struct Event{
  ///the total number of chunks which make up an event
  //static constexpr ChunkCountType nChunks=256;
  static constexpr ChunkCountType nChunks=320;
  //static constexpr ChunkCountType nChunks=360;
  //static constexpr ChunkCountType nChunks=400;
  //static constexpr ChunkCountType nChunks=640;
  //static constexpr ChunkCountType nChunks=100;
  ///the event's data payload
  std::array<Chunk,nChunks> data;
  static_assert(sizeof(data)==nChunks*sizeof(Chunk),"Event data must be contiguous");
  std::array<spinlock,nChunks> chunkLocks;
  ///bitmaps of data chunks which are available
  std::array<std::atomic<bool>,nChunks> chunksReady;
  spinlock transferLock;
  EventID id;
	
  Event(){}
	
  bool isComplete() const{
  //On the theory that chunks should mostly arrive in order, check from
  //last to first to bail out as early as possible in the common case 
  //that a chunk is not ready and hopefully avoid contention with threads
  //doing the useful work of filling in chunks. 
  for(ChunkCountType i=nChunks-1; i>0; i--){
    if(!chunksReady[i])
      return false;
  }
  return chunksReady[0];
}
	
void reset(){
  for(ChunkCountType i=0; i!=nChunks; i++)
    chunksReady[i]=false;
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
	
  Packet(){ memset(&data, 0, sizeof(data)); }
};

static_assert(sizeof(Packet)==sizeof(PacketHeader)+sizeof(Chunk),"Event data must be contiguous");

#endif //EVENT_H
