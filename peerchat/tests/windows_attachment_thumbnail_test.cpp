#include "../windows_attachment_thumbnail.hpp"
#include "../attachment_thumbnail.hpp"
#include <fstream>
#include <iostream>
#include <iterator>

using namespace peerchat;
struct ImageSource final:transfer::Source {
 Blob bytes;
 size_t reads=0;
 explicit ImageSource(const char* path){std::ifstream file(path,std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(file),{});}
 std::uint64_t size()const override{return bytes.size();}
 std::string name()const override{return "thumbnail-probe.png";}
 Blob read(std::uint64_t offset,size_t count)override{
  if(count>transfer::ChunkBytes)throw std::runtime_error("Source read exceeded transfer chunk limit");
  ++reads;
  if(offset>bytes.size())return {};
  const size_t start=static_cast<size_t>(offset);
  return Blob(bytes.begin()+start,bytes.begin()+start+std::min(count,bytes.size()-start));
 }
};
int main(int argc,char** argv){
 if(argc!=2&&argc!=3)return 2;
 ImageSource source(argv[1]);
 Blob jpeg=attachment_thumbnails::create(source);
 if(source.bytes.size()<=transfer::ChunkBytes||source.reads<2)return 9;
 if(jpeg.empty()||jpeg.size()>attachment_thumbnails::MaxBytes||!attachment_thumbnails::valid(jpeg))return 3;
 if(argc==3){std::ofstream output(argv[2],std::ios::binary);output.write(reinterpret_cast<const char*>(jpeg.data()),jpeg.size());if(!output)return 8;}
 Blob corrupt=jpeg;corrupt[0]=0;
 if(attachment_thumbnails::valid(corrupt))return 4;
 corrupt=jpeg;corrupt.resize(16);
 if(attachment_thumbnails::valid(corrupt))return 5;
 corrupt=jpeg;corrupt.resize(attachment_thumbnails::MaxBytes+1);
 if(attachment_thumbnails::valid(corrupt))return 6;
 corrupt=jpeg;
 bool found=false;
 for(size_t at=0;at+8<corrupt.size();++at)if(corrupt[at]==255&&corrupt[at+1]==192){
  corrupt[at+7]=8;corrupt[at+8]=0;found=true;break;
 }
 if(!found||attachment_thumbnails::valid(corrupt))return 7;
 std::cout<<"PASS Windows JPEG thumbnail "<<jpeg.size()<<" bytes; malformed and 2048-pixel JPEGs rejected\n";
}
