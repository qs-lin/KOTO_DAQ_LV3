#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "event.h"

int check_payload( std::vector<unsigned int>, int, int, int );

int main( int argc, char** argv){

  int fileID            = atoi(argv[1]);
  int crateID           = atoi(argv[2]);

  if(crateID !=15 && crateID!=16){
    std::cout << "only crate 15 and crate 16 are available" << std::endl;
    return -1;
  }

  //  ------------- get the data saved on PC -----------------------------
  std::string inputfile = "/local/s1/toyDAQ_0216_v2/myfile_";
  inputfile += std::to_string(fileID);
  inputfile += "_crate_";
  inputfile += std::to_string(crateID);

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
  
  //  ------------- get the simulation information -----------------------
  std::ifstream infile("./simu_table.txt");
  if( infile.fail() ){
    std::cout << "input file does not exist" << std::endl;
    exit(1);
  }

  std::vector<int> init;
  std::vector<int> diff;
  std::vector<int> numb;

  std::string rd_name;
  int rd_init   = 0;
  int rd_diff   = 0;
  int rd_number = 0;

  while(infile>>rd_name>>rd_init>>rd_diff>>rd_number){
    init.push_back(rd_init);
    diff.push_back(rd_diff);
    numb.push_back(rd_number);
    if(( rd_init + rd_diff*rd_number )>1024){
      std::cout << "data size exceeds 1024 threshold " << std::endl;
      exit(1);
    }
  }

  //  ------------- calculate the size of this event ---------------------

  int error = 0;
  int nChunks = 0;
  int nSize   = 0;
  // paylaod size for each adc
  std::vector<int> Size;

  // each adc follows the pattern of arithmetic sequence 
  for(int iadc=0; iadc<16; iadc++){
    int ith   = 0;
    int imod  = fileID%(numb[iadc]); 
    int iSize = 0;
    if(imod==0)
      iSize = init[iadc] + diff[iadc]*numb[iadc]; 
    else
      iSize = init[iadc] + diff[iadc]*imod;

    //6 adc header + 6 adc footer
    //iSize += (6 + 6);
    //if odd, the last words will be EF, FF, FF, F0. 
    //one dummy 0x0000 in the end. 
    /*
    if(iSize%2 != 0)
      iSize++;
    */
    Size.push_back(iSize);
  }// loop of iadc

  for(auto x :  Size){
    //plus energy word, adc header and adc footer
    nSize +=  (x+6*2);
    //if odd, the last words will be EF, FF, FF, F0. 
    //one dummy 0x0000 in the end. 
    if(x%2 != 0)
      nSize++;
  }
  // Ly1 header and footer. Change from 32bit into 16 bit
  nSize += 2*2;

  //std::cout << "nSize: " << nSize << std::endl;

  //  ------------- strip off payload from all chunks --------------------

  nChunks = (nSize%4000==0) ? nSize/4000 : (nSize/4000+1);
  nChunks = nChunks + 1;
  //std::cout << "number of chunks: " << nChunks << std::endl;
  std::vector<unsigned int> payload;
  int fullChunks = nChunks - 2;
  int residual   = nSize - 4000*fullChunks;  //in unit of 16bit word

  for(int iChunk=0; iChunk<fullChunks; iChunk++){
    //reconstruct the 16bit adc energy word from 2 consecutive 8bit  word
    //fullChunks contain the MTU number of words, i.e., 2000 * 32bit word
    //which is 8000 * 8bit word
    for(int i=12;i<8012;i=i+2){
      unsigned int a = (unsigned int)(event[iChunk][i]);
      unsigned int b = (unsigned int)(event[iChunk][i+1]);
      unsigned int c =  a*256 + b;
      payload.push_back(c);
     }
  }

  // the last Chunk does not have any payload.
  for(int iChunk=fullChunks; iChunk<nChunks-1; iChunk++){
    for(int i=12;i<(12+residual*2);i=i+2){
      unsigned int a = (unsigned int)(event[iChunk][i]);
      unsigned int b = (unsigned int)(event[iChunk][i+1]);
      unsigned int c =  a*256 + b;
      payload.push_back(c);
     }
  }

  //std::cout << "last : " << std::hex <<  payload[nSize-2] << std::endl;
  int Layer1_header = (15 << 10);  //001111 [31..26]
  int Layer1_footer = (9 << 10);  //001001 [31..26]

  //  ------------- check the Ly1 header ---------------------------------
  if(payload[0] != Layer1_header)
     error = 1;
  if(payload[1] != fileID)
     error = 1;

  //  ------------- check the Ly1 footer ---------------------------------
  if(payload[nSize-2] != Layer1_footer)
     error = 1;
  if(payload[nSize-1] != 0)
     error = 1;


  //  ------------- check the payload ------------------------------------

  // the first two are Ly1 header
  int start_index = 2;
  for(int iadc=0; iadc<16; iadc++){
    // energy word starts from 2
    //int error_check = check_payload( payload, 2+iadc*interval, energy_size, fileID);
    int error_check = check_payload( payload, start_index, Size[iadc], fileID);
    if(error_check==1)
      error = 1;

    start_index += Size[iadc] + 12;
    start_index = (Size[iadc]%2 != 0) ? start_index+1 : start_index;
  }

  //  ------------- check if event id is correct   -----------------------
  if(crateID==15){
    if(data_id != (fileID*2 -1))
      error = 1;
  }else if(crateID==16){
    if(data_id != (fileID*2 ))
      error = 1;
  }


  //  ------------- check the header of each chunk -----------------------
  for(int iChunk=0; iChunk<nChunks; iChunk++){
    for(int iHead=0; iHead<3; iHead++){
      //reconstruct the 32bit LV2 header word from 4 consecutive 8bit  word
      unsigned int a = (unsigned int)(event[iChunk][4*iHead]);
      unsigned int b = (unsigned int)(event[iChunk][4*iHead+1]);
      unsigned int c = (unsigned int)(event[iChunk][4*iHead+2]);
      unsigned int d = (unsigned int)(event[iChunk][4*iHead+3]);
      unsigned int e = a*16777216 + b*65536 + c*256 + d;
      //1st header = data id
      if(iHead == 0 and e != data_id)
         error = 1;
      //2nd header = chunk id
      if(iHead == 1 and e != iChunk)
         error = 1;
      //3rd header = discriminator
      if(iHead == 2){
        //only last chunk has  discriminator != 0
        if(iChunk == (nChunks-1)){
          //if(e != (1+spill_ID*256+ crateID) )
          //if(e != (spill_ID*256+ crateID) )
          if(e != (0x8000000  + crateID) )
            error = 1;
        }else{
          if(e != 0)
            error = 1;
        }

      }
    }//end of header
  }// end of chunk

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
        std::cout << "error happen at: " << i << " read out is: " << std::hex << payload[start_index+6+i] << " while expected is: " <<  (0x8000 | i ) << std::endl;
      }
      //std::cout << "checked : " << i << " and read out is : "  <<  std::hex << payload[start_index+6+i] << " while desied is: " <<  (0x8000 | i ) << std::endl;

    }

    if(number%2 != 0){
      if(payload[start_index+number+6+6] != 0x0){
        error = 1;
      }
    }




    return error;

  }


