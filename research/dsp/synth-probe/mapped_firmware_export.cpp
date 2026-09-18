#include <cstdio>
#include <fstream>
#include "typed_firmware.h"
int main(int argc,char** argv){
    if(argc!=2)return 1;
    #ifdef SOH3DS_DSP_MAILBOX_DIAGNOSTIC
    auto firmware=makeTypedFirmware(true,true,true,true,true);
#else
    auto firmware=makeTypedFirmware(true,true,true,true);
#endif
    std::ofstream output(argv[1],std::ios::binary);
    for(uint16_t word:firmware.words){output.put(char(word&255));output.put(char(word>>8));}
    if(!output)return 2;
    std::printf("Exported mapped v9 firmware: %zu program words\n",firmware.words.size());
}
