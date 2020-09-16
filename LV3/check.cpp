#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "event.h"

int check_payload( std::vector<unsigned int>, int, int, int ); 


int main( int argc, char** argv){

  int fileID      = atoi(argv[1]); 
  int crateID     = atoi(argv[2]); 
  int spill_ID    = atoi(argv[3]); 
  //std::string inputfile = argv[1];
  //in unit of 16 bit word
  //int energy_size = atoi(argv[3]); 
  int energy_size = 350; 

  if(crateID !=15 && crateID!=16){
    std::cout << "only crate 15 and crate 16 are available" << std::endl;
    return -1;
  }    

  //std::string inputfile = "/local/s1/toyDAQ_0209/myfile_";
  //std::string inputfile = "/local/s1/toyDAQ_0216/myfile_";
  //std::string inputfile = "/local/s1/toyDAQ_0216_v2/myfile_";
  //std::string inputfile = "/local/s1/toyDAQ_0218/spill2/evt_";
  //std::string inputfile = "/local/s1/toyDAQ_0218/spill";
  std::string inputfile = "/local/s1/toyDAQ_0617/spill";
  inputfile += std::to_string(spill_ID);
  inputfile += "/evt_";
  inputfile += std::to_string(fileID);
  inputfile += "_crate_"; 
  inputfile += std::to_string(crateID);

  //std::cout << "inputfile: " << inputfile << std::endl;
  

  int error = 0;
  int nChunks = 0;
  int nSize   = 0;
  nSize = 16*(energy_size + 6 + 6) + 2;
  nChunks = (nSize%4000==0) ? nSize/4000 : (nSize/4000+1);
  nChunks = nChunks + 1; 
  //std::cout << "number of chunks: " << nChunks << std::endl;

  int Layer1_header = (15 << 26);  //001111 [31..26]
  Layer1_header = (Layer1_header | fileID);
  //std::cout << std::hex << Layer1_header << std::endl; 

  std::ifstream rf(inputfile.c_str(), std::ios::out | std::ios::binary);
  if( !rf.is_open() ){
    std::cout <<"input file not exist" << inputfile << std::endl;
    return -1;
  }    

  std::array<Chunk_LV2,Event::nChunks_LV2> event;
  //event id in LV3 header
  EventID data_id = 0;

  rf.read((char*) &data_id, sizeof(data_id));
  rf.read((char*) &event, sizeof(event));

  //  ------------- check if event id is correct   -----------------------
  //std::cout << "data id : " << data_id << std::endl;
  if(crateID==15){
    if(data_id != (fileID*2 -1))
      error = 1;    
  }else if(crateID==16){
    if(data_id != (fileID*2 ))
      error = 1;    
  }    
      

  int chunkID = 0;
  //std::cout << "checking chunk :" << chunkID << std::endl;
  std::vector<unsigned int> value_vec;

  for(int i=0;i<16;i=i+4){

    unsigned int a = (unsigned int)(event[chunkID][i]);
    unsigned int b = (unsigned int)(event[chunkID][i+1]);
    unsigned int c = (unsigned int)(event[chunkID][i+2]);
    unsigned int d = (unsigned int)(event[chunkID][i+3]);
    unsigned int e = a*16777216 + b*65536 + c*256 + d;
    //std::cout << std::hex << e << std::endl;
    value_vec.push_back(e);
  }
  
  //  ------------- check first 4 32bit word for chunk[0] --------------------- 
  
  if(value_vec[0] != data_id)
    error = 1;

  if(value_vec[1] != chunkID)
    error = 1;

  if(value_vec[2] != 0)
    error = 1;

  if(value_vec[3] != Layer1_header)
    error = 1;

  std::vector<unsigned int> payload;
  std::vector<unsigned int> payload_dummy;

  for(int i=16;i<8012;i=i+2){

    unsigned int a = (unsigned int)(event[chunkID][i]);
    unsigned int b = (unsigned int)(event[chunkID][i+1]);
    unsigned int c =  a*256 + b; 
    //std::cout << std::hex << e << std::endl;
    payload.push_back(c);
  }

  /*
  if(payload[0] != 0xffff)
    error = 1;

  std::cout << std::hex << payload[0] << std::endl;

  */



  
  //std::cout << "error: " << error << std::endl;

  //  ------------- go to chunk 1 --------------------------- 
  chunkID = 1;
  //std::cout << "checking chunk :" << chunkID << std::endl;
  std::vector<unsigned int> value_vec_chunk1;

  for(int i=0;i<12;i=i+4){

    unsigned int a = (unsigned int)(event[chunkID][i]);
    unsigned int b = (unsigned int)(event[chunkID][i+1]);
    unsigned int c = (unsigned int)(event[chunkID][i+2]);
    unsigned int d = (unsigned int)(event[chunkID][i+3]);
    unsigned int e = a*16777216 + b*65536 + c*256 + d;
    //std::cout << std::hex << e << std::endl;
    value_vec_chunk1.push_back(e);
  }

  for(int i=12;i<3606;i=i+2){
  //for(int i=12;i<3606;i=i+2){

    unsigned int a = (unsigned int)(event[chunkID][i]);
    unsigned int b = (unsigned int)(event[chunkID][i+1]);
    unsigned int c =  a*256 + b; 
    //std::cout << std::hex << e << std::endl;
    payload.push_back(c);
  }

  for(int i=3604;i<8012;i=i+2){
  //for(int i=12;i<3606;i=i+2){

    unsigned int a = (unsigned int)(event[chunkID][i]);
    unsigned int b = (unsigned int)(event[chunkID][i+1]);
    unsigned int c =  a*256 + b; 
    //std::cout << std::hex << e << std::endl;
    payload_dummy.push_back(c);
  }

  for(int i=0;i<payload_dummy.size();i++){
    if(payload_dummy[i] != 0)
      error = 1;    


  }    


  //std::cout << payload.size() << std::endl;
  if(payload[0] != 0xffff)
    error = 1;

  /*
  std::cout << std::hex << payload[0] << std::endl;
  std::cout << std::hex << payload[1] << std::endl;
  std::cout << std::hex << payload[2] << std::endl;
  std::cout << std::hex << payload[3] << std::endl;
  std::cout << std::hex << payload[4] << std::endl;
  std::cout << std::hex << payload[5] << std::endl;
  std::cout << std::hex << payload[6] << std::endl;
  std::cout << std::hex << payload[355] << std::endl;
  std::cout << std::hex << payload[356] << std::endl;
  std::cout << std::hex << payload[357] << std::endl;
  std::cout << std::hex << payload[358] << std::endl;
  std::cout << std::hex << payload[359] << std::endl;
  std::cout << std::hex << payload[360] << std::endl;
  std::cout << std::hex << payload[361] << std::endl;
  //std::cout << std::hex << payload[362] << std::endl;
  
  std::cout << std::hex << payload[5790] << std::endl;
  std::cout << std::hex << payload[5791] << std::endl;
  std::cout << std::hex << payload[5792] << std::endl;
  std::cout << std::hex << payload[5793] << std::endl;
  */

  if(payload[5792]!=0x2400)
    error = 1;
  if(payload[5793]!=0x0)
    error = 1;
  if(payload[5794]!=0x0)
    error = 1;


  /*
  for(int i=0;i<5792;i++){
    error = check_payload(payload,i*362,350,fileID);
  }
  */


  int error_payload = 1;
  for(int i=0;i<16;i++){
    error_payload = check_payload(payload,i*362,350,fileID);
    if(error_payload !=0)
      error = 1;
  }
  
  
  //  ------------- check first 3 32bit word for chunk[1] --------------------- 

  if(value_vec_chunk1[0] != data_id)
    error = 1;

  if(value_vec_chunk1[1] != chunkID)
    error = 1;

  if(value_vec_chunk1[2] != 0)
    error = 1;

  //std::cout << "error: " << error << std::endl;
  
  //  ------------- go to chunk 2 --------------------------- 

  chunkID = 2;
  //std::cout << "checking chunk :" << chunkID << std::endl;
  std::vector<unsigned int> value_vec_chunk2;

  for(int i=0;i<12;i=i+4){

    unsigned int a = (unsigned int)(event[chunkID][i]);
    unsigned int b = (unsigned int)(event[chunkID][i+1]);
    unsigned int c = (unsigned int)(event[chunkID][i+2]);
    unsigned int d = (unsigned int)(event[chunkID][i+3]);
    unsigned int e = a*16777216 + b*65536 + c*256 + d;
    //std::cout << std::hex << e << std::endl;
    value_vec_chunk2.push_back(e);
  }

  //  ------------- check first 3 32bit word for chunk[2] --------------------- 

  if(value_vec_chunk2[0] != data_id)
    error = 1;

  if(value_vec_chunk2[1] != chunkID)
    error = 1;

  
  //int discriminator = (0x8000000 + crateID);
  //ff_tx_data    <= X"000" & "000" & spill_id & crate_id;
  int discriminator = (spill_ID*256+ crateID);
  //std::cout << "dis: " << std::hex << discriminator << std::endl;
  if(value_vec_chunk2[2] != discriminator)
    error = 1;

  //std::cout << "error: " << error << std::endl;

  if(error != 0){
    std::cerr << "error happen at: " << inputfile << std::endl;
    return -1;
  }


  return 0;

}


  int check_payload( std::vector<unsigned int> payload, int start_index, int number, int event_id ){
    int error = 0 ;
    //check first 5 headers
    for(int i=0;i<5;i++){
      if(payload[start_index+i] != 0xffff){
        error = 1;
      }  
    }      

    //check 6th header
    if(payload[start_index+5] != (0xC000 | event_id)){
      error = 1;
    }
    //std::cout << std::hex << "paylaod[5]: " << payload[start_index+5] << " vs: " << (0xC000 | event_id) << std::endl;




    //check 6 footers
    for(int i=0;i<6;i++){
      //number=event_size 6=#ofheader
      if(payload[start_index+number+6+i] != 0x6fff){
        error = 1;
      } 
    }    
    //
    //check all energy words 
    
    for(int i=0;i<number;i++){
      //number=event_size 6=#ofheader
      if(payload[start_index+6+i] != (0x8000 | i)){  
        error = 1;
        std::cout << "error happen at: " << i << " read out is: " << std::hex << payload[start_index+6+i] << " while desied is: " <<  (0x8000 | i ) << std::endl; 
      } 
      //std::cout << "checked : " << i << " and read out is : "  <<  std::hex << payload[start_index+6+i] << " while desied is: " <<  (0x8000 | i ) << std::endl;

    }    
    



    return error;

  }    



