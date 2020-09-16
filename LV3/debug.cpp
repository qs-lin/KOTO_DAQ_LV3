#include <iostream>
#include <algorithm>
#include <fstream>
#include <vector>

int main(){


  std::ifstream infile("debug.txt");
  int index = 0;
  int repeated = 0;
  std::vector<int> value;
  while(infile >> index){
    value.push_back(index);  
  }
  std::sort(value.begin(),value.end());
  for(int i=1;i<value.size();i++){
    if(value[i] != (value[i-1]+1)){
      std::cout << "previous one: " << value[i-1] << " current one: " << value[i] << std::endl;
      repeated++;
    }
  }    

  std::cout << "total: " << value.size() << std::endl;
  std::cout << "repeated: " << repeated << std::endl;


return 0;
}    


