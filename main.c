#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>


/***************************
 *  Main structures and    *
 *  Global variables       *
 *                         *
 ***************************/
struct context {
    uint64_t rflags;
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rip;
};
struct context_wrapper {
    struct  context * ctx_ptr;
    int     stack_size;
    void *  mem_ptr;
};
struct thread {
    struct context_wrapper * wrapp;
    bool is_free;
    bool is_done;
    void * result;
};

static const int CONTEXT_SIZE           = sizeof(struct context);
static const int CONTEXT_WRAPPER_SIZE   = sizeof(struct context_wrapper);

#define MAX_THREADS 64
static struct thread thread_pool[MAX_THREADS];
static atomic_bool IS_LOCED             = false;

static const int tid0                   = 0;
static bool is_debug_mode               = true;



/***********************************
 *  Synchronization primitives     *
 *  For not signal-safe functions  *
 *  like as: (printf, malloc, ...) *
 *                                 *
 ***********************************/
static void switch_next(int tid);
void dummy_lock()    {
    const atomic_bool NOT_LOCKED = false;
    while (atomic_compare_exchange_strong(&IS_LOCED, &NOT_LOCKED, true));
}
void dummy_unlock()  { IS_LOCED = false; }



/***********************************
 *  Low level context switching    *
 *  handlers, only for x86-64      *
 *                                 *
 ***********************************/
static void switch_thread(struct context_wrapper * from, struct context_wrapper * to) {
    void __switch_thread__(void ** prev, void * next); // assembly function
    __switch_thread__((void *)(&from->ctx_ptr), (void *)to->ctx_ptr);
}
static void switch_next(int tid) {
    // optimization for switching inner critical section
    if (IS_LOCED) return;

    int new_tid = tid + 1;

    dummy_lock();
    while (new_tid != tid) {
        new_tid = new_tid % MAX_THREADS;
        if (!thread_pool[new_tid].is_free && !thread_pool[new_tid].is_done) break;
        new_tid++;
    }
    if (is_debug_mode)
        printf("[Switching from tid=%d to new_tid=%d]\n", tid, new_tid);

    if (new_tid != tid) {
        dummy_unlock();
        switch_thread(thread_pool[tid].wrapp, thread_pool[new_tid].wrapp);
    } else {
        dummy_unlock();
    }
}
static void __done_task__(void * result, int tid) {
    // is marking as finished
    dummy_lock();
    thread_pool[tid].is_done = true;
    thread_pool[tid].result = result;
    dummy_unlock();

    if (is_debug_mode) {
        dummy_lock();
        printf("Thread tid=%d is exit with result pointer: [%p][%lu]\n", tid, result, (uint64_t)result);
        dummy_unlock();
    }

    switch_thread(thread_pool[tid].wrapp, thread_pool[tid0].wrapp);

    dummy_lock();
    printf("Don't exist threads or internal error\n");
    dummy_unlock();

    exit(1);
}
/**
 * Real exit of thread, is called by previously manually saved rip registry on stack
 */
static void __exit_handler__(void) {
    // rax - register filled by entry point
    register uint64_t rax __asm__("%rax");
    // rsp == tid - is manually saved value on stack
    register uint64_t rsp __asm__("%rsp");
    __done_task__((void *)rax, *((int *)rsp + 2));
}



/***********************************
 *  Handlers for setup and teardown*
 *  context of threads             *
 *                                 *
 ***********************************/
struct context_wrapper * init_stack(void *(*entry_point)(void*,int), void * arg, uint64_t tid) {
    const int STACK_SIZE = 4 * (1 << 20); // 4 MiB
    if (entry_point == NULL)
        return NULL;

    dummy_lock();
    struct context_wrapper * wrapp = malloc(CONTEXT_WRAPPER_SIZE);
    dummy_unlock();

    if (wrapp == NULL)
        return NULL;

    struct context ctx  = { 0,0,0,0,0,0,0,0,tid,(uint64_t)entry_point };
    if (arg != NULL)
        ctx.rdi = (uint64_t)arg;

    wrapp->stack_size   = STACK_SIZE;
    dummy_lock();
    wrapp->mem_ptr      = malloc(STACK_SIZE);
    dummy_unlock();

    if (wrapp->mem_ptr == NULL)
        return NULL;

    uint8_t * tid_ptr   = (uint8_t *)wrapp->mem_ptr + STACK_SIZE - sizeof(uint64_t);
    *(uint64_t *)tid_ptr= tid;

    uint8_t * exit_rip  = tid_ptr - sizeof(uint64_t);
    *(uint64_t *)exit_rip = (uint64_t)__exit_handler__;

    wrapp->ctx_ptr      = (struct context *)(exit_rip - CONTEXT_SIZE);
    *wrapp->ctx_ptr     = ctx;

    return wrapp;
}
static void destroy_stack(struct context_wrapper * wrapp) {
    dummy_lock();
    free(wrapp->mem_ptr);
    free(wrapp);
    dummy_unlock();
}



/***************************************
 *  Global constructor. Must call      *
 *  before any other functions         *
 *                                     *
 ***************************************/
void initialize(bool is_debug) {
    static struct context_wrapper wrapp_main = {0};
    is_debug_mode = is_debug;

    for (int i = 0; i < MAX_THREADS; i++)
        thread_pool[i].is_free = true;

    thread_pool[tid0].wrapp    = &wrapp_main;
    thread_pool[tid0].is_free  = false;
    thread_pool[tid0].is_done  = false;
    thread_pool[tid0].result   = NULL;
}



/***********************
 *    API              *
 *                     *
 ***********************/
int create_thread(void *(*entry_point)(void*,int), void * arg) {
    int tid = 0;

    dummy_lock();
    for (int i = 1; i < MAX_THREADS; i++)
        if (thread_pool[i].is_free) {
            thread_pool[i].is_free = false;
            tid = i; break;
        }
    dummy_unlock();

    if (tid == 0) {
        printf("Max count threads=%d\n", MAX_THREADS);
        exit(1);
    }

    thread_pool[tid].wrapp    = init_stack(entry_point, arg, tid);
    thread_pool[tid].is_done  = false;
    thread_pool[tid].result   = NULL;
    return tid;
}

void * join_thread(int tid, int cur_tid) {
    while (1) {
        dummy_lock();
        if (!thread_pool[tid].is_free && !thread_pool[tid].is_done) {
            dummy_unlock();
            switch_next(cur_tid);
        } else { break; }
    }

    void * result = thread_pool[tid].result;
    if (!thread_pool[tid].is_free && thread_pool[tid].is_done) {
        destroy_stack(thread_pool[tid].wrapp);
        thread_pool[tid].is_free = true;
    }

    dummy_unlock();
    return result;
}



void * entry_point(void * arg, int tid) {
    // BEST PRACTICE OF USING switch_next
    #define SWITCH_NEXT() switch_next(tid);

    // arg - can be NULL
    int * xp = (int *)arg;

    for (int i = 0; i < 3; i++) {
        dummy_lock();
        // tid - thread id of current thread
        printf("Hello from tid=%d, got arg x=%d\n", tid, *xp);

        // BAD PRACTICE:
        // if you switch inner critical section
        // switch request will be ignored
        dummy_unlock();

        // BEST PRACTICE:
        // Switch available only outside critical section
        SWITCH_NEXT();
    }

    // can return pointer to any structure
    return (void *)xp;
    // Or exit status as [8;64]-bit value, example:
    //
    // int exist_status = 42;
    // return (void *)exist_status;
}

int main(int argc, char ** argv) {
    // it is supporting only x86-64 architecture
    if (sizeof(void *) == sizeof(uint32_t)) {
        printf("Sorry, don't support 32-bit architecture\n");
        exit(1);
    }

    bool is_debug = false;
    if (argc == 2 && strcmp(argv[argc-1], "--debug"))
         is_debug = true;

    // initialize global structures and
    // set flag of [debug mode]
    //
    // debug mode - need for output logs
    // about switching
    // and terminating of threads
    initialize(is_debug);

    int x1 = 10; int x2 = 20;
    int x3 = 30; int x4 = 40;

    // argument can be NULL, API similar to POSIX threads
    int tid1 = create_thread(&entry_point, (void *)&x1);
    int tid2 = create_thread(&entry_point, (void *)&x2);
    int tid3 = create_thread(&entry_point, (void *)&x3);
    int tid4 = create_thread(&entry_point, (void *)&x4);

    // tid0 - thread id of main thread
    switch_next(tid0);

    // params [tid_for_waiting], [current_tid] is required
    //
    // 'join_thread' can call in other threads
    // NOT ONLY in main thread
    void * res4 = join_thread(tid4, tid0);
    void * res3 = join_thread(tid3, tid0);
    void * res2 = join_thread(tid2, tid0);
    void * res1 = join_thread(tid1, tid0);

    printf("tid=%d result: %d\n", tid1, *(int *)res1);
    printf("tid=%d result: %d\n", tid2, *(int *)res2);
    printf("tid=%d result: %d\n", tid3, *(int *)res3);
    printf("tid=%d result: %d\n", tid4, *(int *)res4);

    return 0;
}
