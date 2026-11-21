#include "../src/assets/visn.h"
#include <cstdio>
#include <vector>
int main(int argc,char**argv){
    FILE*f=fopen(argv[1],"rb"); std::vector<uint8_t> d; int c; while((c=fgetc(f))!=EOF) d.push_back(uint8_t(c)); fclose(f);
    std::vector<uint16_t> o; int w,h;
    if(!xa::visnDecode(d.data(),d.size(),o,w,h)){fprintf(stderr,"fail\n");return 1;}
    FILE*g=fopen(argv[2],"wb"); fwrite(o.data(),2,o.size(),g); fclose(g); return 0;
}
