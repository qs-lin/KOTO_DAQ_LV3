#include <fstream>
#include <iostream>
#include <string>
#include "event.h"


int main( int argc, char** argv){

  std::string inputfile = argv[1];
  int chunkID = atoi(argv[2]);


/*
  //int test = 57344;
  int test = 40960;
  std::cout << (test>>14) << std::endl;
  return 0;
*/

  std::ifstream rf(inputfile.c_str(), std::ios::out | std::ios::binary);

  //std::array<Chunk_8,Event::nChunks> data;
  //std::array<uint8_t,10000> data;
  //unsigned char data[8012]; 
  //std::cout << sizeof(data) << std::endl; 
  std::array<Chunk_LV2,Event::nChunks_LV2> event;
  EventID id = 0;

  rf.read((char*) &id, sizeof(id));
  rf.read((char*) &event, sizeof(event));

  std::cout << "event id : " << id << std::endl;
  
  //for(int i=3500;i<3604;i=i+4){
  //for(int i=3900;i<3924;i=i+4){
  //for(int i=3500;i<3624;i=i+4){
  //for(int i=8000;i<8012;i=i+4){

  int num_header = 0;
  //for(int i=0;i<8012;i=i+2){
  for(int i=0;i<128;i=i+2){

    unsigned int a = (unsigned int)(event[chunkID][i]);
    unsigned int b = (unsigned int)(event[chunkID][i+1]);
    //unsigned int c = (unsigned int)(event[chunkID][i+2]);
    //unsigned int d = (unsigned int)(event[chunkID][i+3]);
    //unsigned int e = a*16777216 + b*65536 + c*256 + d;
    unsigned int e = a*256 + b;
    //if( (e & 0xC000)==0xC000 )
    //if( (e>>12) == 6 ) 
      //num_header ++;
    std::cout << ("unsigned int: ") << std::hex << e << std::endl;
    //std::cout << ("unsigned int: ") << std::hex << a << std::endl;
    //std::cout << ("unsigned int: ") << std::hex << b << std::endl;
  }
  //std::cout << "# of header: " << num_header << std::endl;
  

  /*
  for(int i=8000;i<8012;i++){
    unsigned int a = (unsigned int)(event[chunkID][i]);
    std::cout << ("unsigned int: ") << std::hex << a << std::endl;
  }
  */
  //for(int i=0;i<8012;i++){
    
    //const char *str  = &data[i];
    //int y = atoi(str); 
    //std::cout << "y: "<<y << std::endl;
    //printf("x: %x\n",data[i]);
   
    //std::cout << data[i] << std::endl;
    //std::cout << "i: ---- " << i << std::endl;
    //printf("x: %x\n",data[i]);


    //uint8_t b = (uint8_t)(data[i]);

    //std::cout << ("uint8_t: ") << b << std::endl;
    //printf("unsigned char: %x\n",data[i]);



  /*
  unsigned char ch = 40;
  std::cout << sizeof(ch) << std::endl;

  unsigned int uin = (unsigned int) ch;
  std::cout << sizeof(uin) << std::endl;
  std::cout << uin << std::endl;

  uint8_t us8 = (uint8_t) ch;
  std::cout << sizeof(us8) << std::endl;
  std::cout << us8 << std::endl;

  */
   


  return 0;

}
