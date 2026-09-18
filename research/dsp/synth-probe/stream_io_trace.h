#pragma once
#include <cstdint>
#ifdef SOH3DS_DSP_STREAM_IO_TRACE
void SohDspStreamIoTrace(const char*,unsigned,unsigned,int32_t);
#define SOH_DSP_TRACE_IO(stage,word,count,result) SohDspStreamIoTrace(stage,word,count,result)
#else
#define SOH_DSP_TRACE_IO(stage,word,count,result) ((void)0)
#endif
