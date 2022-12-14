#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>

struct context {
    uint64_t rflags;
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rdi;
    uint64_t rip;
};
struct context_wrapper {
    struct  context * ctx_ptr;
    int     stack_size;
    void *  mem_ptr;
};
static const int CONTEXT_SIZE           = sizeof(struct context);
static const int CONTEXT_WRAPPER_SIZE   = sizeof(struct context_wrapper);

struct thread {
    struct context_wrapper * wrapp;
    bool is_free;
    bool is_done;
    void * result;
};

#define                     MAX_THREADS 64
static struct thread        thread_pool[MAX_THREADS];

static atomic_bool          IS_LOCED;
static void dummy_lock()    {
    const atomic_bool LOCKED = true;
    while (atomic_compare_exchange_strong(&IS_LOCED, &LOCKED, !LOCKED));
}
static void dummy_unlock()  { IS_LOCED = false; }

static const int tid0       = 0;
static bool is_debug_mode   = true;


static void switch_thread(struct context_wrapper * from, struct context_wrapper * to) {
    void __switch_thread__(void ** prev, void * next);
    __switch_thread__((void *)(&from->ctx_ptr), (void *)to->ctx_ptr);
}
static void switch_next(int tid) {
    if (IS_LOCED) return;
    int new_tid = tid + 1;

    dummy_lock();
    while (new_tid != tid) {
        new_tid = new_tid % MAX_THREADS;
        if (!thread_pool[new_tid].is_free && !thread_pool[new_tid].is_done) break;
        new_tid++;
    }
    if (is_debug_mode)
        printf("tid=%d, new_tid=%d\n", tid, new_tid);

    if (new_tid != tid) {
        dummy_unlock();
        switch_thread(thread_pool[tid].wrapp, thread_pool[new_tid].wrapp);
    } else {
        dummy_unlock();
    }
}

static void __done_task__(void * result, int tid) {
    dummy_lock();
    thread_pool[tid].is_done = true;
    thread_pool[tid].result = result;
    dummy_unlock();

    if (is_debug_mode) {
        dummy_lock();
        printf("Some task is exit, result: [%p][%lu], tid: %d\n", result, (uint64_t)result, tid);
        dummy_unlock();
    }
    switch_thread(thread_pool[tid].wrapp, thread_pool[tid0].wrapp);

    dummy_lock();
    printf("Don't exist threads or scheduler error\n");
    dummy_unlock();

    exit(1);
}
static void __exit_handler__(void) {
    register uint64_t rax __asm__("%rax");
    register uint64_t rsp __asm__("%rsp");
    __done_task__((void *)rax, *((int *)rsp + 2));
}
struct context_wrapper * init_stack(void *(*entry_point)(void*), void * arg, uint64_t tid) {
    const int STACK_SIZE = 4 * (1 << 20); // 4 MiB
    if (entry_point == NULL)
        return NULL;

    dummy_lock();
    struct context_wrapper * wrapp = malloc(CONTEXT_WRAPPER_SIZE);
    dummy_unlock();

    if (wrapp == NULL)
        return NULL;

    struct context ctx  = { 0,0,0,0,0,0,0,0,(uint64_t)entry_point };
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
void destroy_stack(struct context_wrapper * wrapp) {
    dummy_lock();
    free(wrapp->mem_ptr);
    free(wrapp);
    dummy_unlock();
}

void initialize() {
    static struct context_wrapper wrapp_main = {0};
    for (int i = 0; i < MAX_THREADS; i++)
        thread_pool[i].is_free = true;

    thread_pool[tid0].wrapp    = &wrapp_main;
    thread_pool[tid0].is_free  = false;
    thread_pool[tid0].is_done  = false;
    thread_pool[tid0].result   = NULL;
}

int create_thread(void *(*entry_point)(void*), void * arg) {
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
        } else {
            break;
        }
    }

    void * result = thread_pool[tid].result;
    if (!thread_pool[tid].is_free && thread_pool[tid].is_done) {
        destroy_stack(thread_pool[tid].wrapp);
        thread_pool[tid].is_free = true;
    }

    dummy_unlock();
    return result;
}



void * entry_point1(void * arg) {
    int * xp = (int *)arg;
    for (int i = 0; i < 3; i++) {
        dummy_lock();
        printf("Hello, world from thread 1, got arg x=%d\n", *xp);
        dummy_unlock();

        switch_next(1);
    }
    return (void *)xp;
}
void * entry_point2(void * arg) {
    int * xp = (int *)arg;
    for (int i = 0; i < 3; i++) {
        dummy_lock();
        printf("Hello, world from thread 2, got arg x=%d\n", *xp);
        dummy_unlock();

        switch_next(2);
    }
    return (void *)xp;
}
void * entry_point3(void * arg) {
    int * xp = (int *)arg;
    for (int i = 0; i < 3; i++) {
        dummy_lock();
        printf("Hello, world from thread 3, got arg x=%d\n", *xp);
        dummy_unlock();

        switch_next(3);
    }
    return (void *)xp;
}
void * entry_point4(void * arg) {
    int * xp = (int *)arg;
    for (int i = 0; i < 3; i++) {
        dummy_lock();
        printf("Hello, world from thread 4, got arg x=%d\n", *xp);
        dummy_unlock();

        switch_next(4);
    }
    return (void *)xp;
}
void * entry_point5(void * arg) {
    int * xp = (int *)arg;
    for (int i = 0; i < 3; i++) {
        dummy_lock();
        printf("Hello, world from thread 5, got arg x=%d\n", *xp);
        dummy_unlock();

        switch_next(5);
    }
    return (void *)xp;
}

int main() {
    if (sizeof(void *) == sizeof(uint32_t)) {
        printf("Sorry, don't support 32-bit architecture\n");
        exit(1);
    }

    initialize();
    is_debug_mode = true;

    int x1 = 10;
    int x2 = 20;
    int x3 = 30;
    int x4 = 40;
    int x5 = 50;

    create_thread(&entry_point1, (void *)&x1);
    create_thread(&entry_point2, (void *)&x2);
    create_thread(&entry_point3, (void *)&x3);
    create_thread(&entry_point4, (void *)&x4);
    create_thread(&entry_point5, (void *)&x5);

    switch_next(tid0);

    void * res5 = join_thread(5, 0);
    void * res4 = join_thread(4, 0);
    void * res3 = join_thread(3, 0);
    void * res2 = join_thread(2, 0);
    void * res1 = join_thread(1, 0);

    printf("finish main\n");
    printf("th1 result: %d\n", *(int *)res1);
    printf("th2 result: %d\n", *(int *)res2);
    printf("th3 result: %d\n", *(int *)res3);
    printf("th4 result: %d\n", *(int *)res4);
    printf("th5 result: %d\n", *(int *)res5);

    return 0;
}
