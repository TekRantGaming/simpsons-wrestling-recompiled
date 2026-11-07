/* android_fiber.c -- psx_fiber.h for Android.
 *
 * psxrecomp's POSIX fiber backend (runtime/src/psx_fiber.c) uses ucontext
 * (getcontext / makecontext / swapcontext), which Android's C library does
 * not have. The Android build compiles this file in its place: the same API
 * on a minimal stack switch that saves the registers the platform ABI says a
 * function must preserve (the rest are already saved by the compiler around
 * the call), stores the stack pointer, loads the other fiber's and returns
 * on its stack.
 *
 * Cooperative and single-threaded, exactly like the ucontext backend: one
 * fiber runs at a time and every switch is an explicit psx_fiber_switch().
 *
 * Supported: arm64-v8a, armeabi-v7a (TV boxes and TVs), x86_64 (emulator). */
#include "psx_fiber.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* swr_ctx_swap(&from->sp, to->sp): save callee-saved registers on the current
 * stack, store the stack pointer in *from_sp, switch to to_sp, restore that
 * fiber's registers and return into it. */
void swr_ctx_swap(void** from_sp, void* to_sp);

#if defined(__aarch64__)
/* x19-x28, x29 (fp), x30 (lr), d8-d15: 20 slots of 8 bytes = 160 bytes. */
#define SWR_FRAME_BYTES 160
#define SWR_LR_SLOT     11      /* x30, in 8-byte slots from the frame base */
__asm__(
    ".text\n"
    ".balign 16\n"
    ".global swr_ctx_swap\n"
    ".hidden swr_ctx_swap\n"
    ".type swr_ctx_swap, %function\n"
    "swr_ctx_swap:\n"
    "    sub  sp, sp, #160\n"
    "    stp  x19, x20, [sp, #0]\n"
    "    stp  x21, x22, [sp, #16]\n"
    "    stp  x23, x24, [sp, #32]\n"
    "    stp  x25, x26, [sp, #48]\n"
    "    stp  x27, x28, [sp, #64]\n"
    "    stp  x29, x30, [sp, #80]\n"
    "    stp  d8,  d9,  [sp, #96]\n"
    "    stp  d10, d11, [sp, #112]\n"
    "    stp  d12, d13, [sp, #128]\n"
    "    stp  d14, d15, [sp, #144]\n"
    "    mov  x2, sp\n"
    "    str  x2, [x0]\n"
    "    mov  sp, x1\n"
    "    ldp  x19, x20, [sp, #0]\n"
    "    ldp  x21, x22, [sp, #16]\n"
    "    ldp  x23, x24, [sp, #32]\n"
    "    ldp  x25, x26, [sp, #48]\n"
    "    ldp  x27, x28, [sp, #64]\n"
    "    ldp  x29, x30, [sp, #80]\n"
    "    ldp  d8,  d9,  [sp, #96]\n"
    "    ldp  d10, d11, [sp, #112]\n"
    "    ldp  d12, d13, [sp, #128]\n"
    "    ldp  d14, d15, [sp, #144]\n"
    "    add  sp, sp, #160\n"
    "    ret\n"
    ".size swr_ctx_swap, .-swr_ctx_swap\n");

#elif defined(__arm__)
/* d8-d15 (64 bytes) below r4-r12, lr (40 bytes): 104 bytes, 8-byte aligned.
 * Assembled as ARM code; `bx lr` returns to ARM or Thumb callers alike. */
#define SWR_FRAME_BYTES 104
#define SWR_LR_OFFSET   100     /* byte offset of lr from the frame base */
__asm__(
    ".text\n"
    ".balign 4\n"
    ".arm\n"
    ".global swr_ctx_swap\n"
    ".hidden swr_ctx_swap\n"
    ".type swr_ctx_swap, %function\n"
    "swr_ctx_swap:\n"
    "    push  {r4-r12, lr}\n"
    "    vpush {d8-d15}\n"
    "    str   sp, [r0]\n"
    "    mov   sp, r1\n"
    "    vpop  {d8-d15}\n"
    "    pop   {r4-r12, lr}\n"
    "    bx    lr\n"
    ".size swr_ctx_swap, .-swr_ctx_swap\n");

#elif defined(__x86_64__)
/* rbp, rbx, r12-r15 then the return address. */
#define SWR_FRAME_BYTES 56
__asm__(
    ".text\n"
    ".balign 16\n"
    ".global swr_ctx_swap\n"
    ".hidden swr_ctx_swap\n"
    ".type swr_ctx_swap, @function\n"
    "swr_ctx_swap:\n"
    "    pushq %rbp\n"
    "    pushq %rbx\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    movq  %rsp, (%rdi)\n"
    "    movq  %rsi, %rsp\n"
    "    popq  %r15\n"
    "    popq  %r14\n"
    "    popq  %r13\n"
    "    popq  %r12\n"
    "    popq  %rbx\n"
    "    popq  %rbp\n"
    "    ret\n"
    ".size swr_ctx_swap, .-swr_ctx_swap\n");

#else
#error "android_fiber.c: unsupported Android ABI (arm64-v8a, armeabi-v7a, x86_64)"
#endif

typedef struct swr_fiber {
    void*           sp;       /* saved stack pointer while suspended */
    void*           stack;    /* NULL for the thread's own fiber */
    psx_fiber_entry entry;
    void*           arg;
} swr_fiber;

static swr_fiber* s_current = NULL;

#define SWR_MIN_STACK (64u * 1024u)

/* First code a new fiber runs: s_current is already the new fiber. */
static void swr_fiber_boot(void)
{
    swr_fiber* f = s_current;
    f->entry(f->arg);
    /* A BIOS thread entry never returns (see psx_fiber.c). */
    abort();
}

psx_fiber_t psx_fiber_convert_thread(void)
{
    if (!s_current) {
        swr_fiber* f = (swr_fiber*)calloc(1, sizeof(*f));
        if (!f) return NULL;
        s_current = f;
    }
    return (psx_fiber_t)s_current;
}

psx_fiber_t psx_fiber_current(void) { return (psx_fiber_t)s_current; }

psx_fiber_t psx_fiber_create(size_t stack_size, psx_fiber_entry entry, void* arg)
{
    if (stack_size < SWR_MIN_STACK) stack_size = SWR_MIN_STACK;
    swr_fiber* f = (swr_fiber*)calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->stack = malloc(stack_size);
    if (!f->stack) { free(f); return NULL; }
    f->entry = entry;
    f->arg   = arg;

    uintptr_t top = ((uintptr_t)f->stack + stack_size) & ~(uintptr_t)15;
#if defined(__x86_64__)
    /* After `ret` pops the boot address, rsp must be 8 mod 16, as if
     * swr_fiber_boot had been called. */
    top -= 8;
#endif
    uint8_t* frame = (uint8_t*)(top - SWR_FRAME_BYTES);
    memset(frame, 0, SWR_FRAME_BYTES);
#if defined(__aarch64__)
    ((uint64_t*)frame)[SWR_LR_SLOT] = (uint64_t)(uintptr_t)swr_fiber_boot;
#elif defined(__arm__)
    *(uint32_t*)(frame + SWR_LR_OFFSET) = (uint32_t)(uintptr_t)swr_fiber_boot;
#elif defined(__x86_64__)
    ((uint64_t*)frame)[6] = (uint64_t)(uintptr_t)swr_fiber_boot;
#endif
    f->sp = frame;
    return (psx_fiber_t)f;
}

void psx_fiber_switch(psx_fiber_t target)
{
    swr_fiber* to   = (swr_fiber*)target;
    swr_fiber* from = s_current;
    if (!to || to == from || !from) return;
    s_current = to;
    swr_ctx_swap(&from->sp, to->sp);
    /* Resumed: whoever switched back set s_current to `from`. */
}

void psx_fiber_destroy(psx_fiber_t fiber)
{
    swr_fiber* f = (swr_fiber*)fiber;
    if (!f || f == s_current) return;
    free(f->stack);
    free(f);
}
