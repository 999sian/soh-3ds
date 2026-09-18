// Freestanding QEMU-only runtime for the compact pipeline regression.
extern "C" void* memcpy(void* d,const void* s,size_t n){auto* x=(unsigned char*)d;auto* y=(const unsigned char*)s;while(n--)*x++=*y++;return d;}
extern "C" void* memset(void* d,int v,size_t n){auto* x=(unsigned char*)d;while(n--)*x++=v;return d;}
extern "C" int memcmp(const void* a,const void* b,size_t n){auto*x=(const unsigned char*)a;auto*y=(const unsigned char*)b;while(n--){if(*x!=*y)return *x-*y;x++;y++;}return 0;}
extern "C" void __aeabi_unwind_cpp_pr0(){} extern "C" void __aeabi_unwind_cpp_pr1(){}
extern "C" void _start(){correctness();
 register uint32_t r0 asm("r0")=1;register void* r1 asm("r1")=failures;register uint32_t r2 asm("r2")=24;
 asm volatile("mov r7,#4\nsvc #0":"+r"(r0):"r"(r1),"r"(r2):"r7","memory");
 r0=failures[0]!=0;asm volatile("mov r7,#1\nsvc #0"::"r"(r0):"r7","memory");__builtin_unreachable();}
