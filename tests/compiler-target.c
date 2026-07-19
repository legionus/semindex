#if defined(__x86_64__)
#define STACK_POINTER "rsp"
#else
#define STACK_POINTER "esp"
#endif

register unsigned long current_stack_pointer asm(STACK_POINTER);
