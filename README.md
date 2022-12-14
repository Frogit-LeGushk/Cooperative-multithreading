## Cooperative multithreading
### How look entry point
```c
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
```
### Step 1: how initialize
```c
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
```
### Step 2: how add tasks
```c
int x1 = 10; int x2 = 20;
int x3 = 30; int x4 = 40;

// argument can be NULL, API similar to POSIX threads
int tid1 = create_thread(&entry_point, (void *)&x1);
int tid2 = create_thread(&entry_point, (void *)&x2);
int tid3 = create_thread(&entry_point, (void *)&x3);
int tid4 = create_thread(&entry_point, (void *)&x4);
```
### Step 3: how switch between threads
```c
// tid0 - thread id of main thread
switch_next(tid0);
```
### Step 4: how wait threads
```c
// params [tid_for_waiting], [current_tid] is required
//
// 'join_thread' can call in other threads
// NOT ONLY in main thread
void * res4 = join_thread(tid4, tid0);
void * res3 = join_thread(tid3, tid0);
void * res2 = join_thread(tid2, tid0);
void * res1 = join_thread(tid1, tid0);
```
### How start and result:
```console
acool4ik@FORG-host:~/Desktop/MULTITHREADING$ make && ./main 
gcc -c -Wall -Wextra -Wswitch-enum -pedantic -ggdb -pg -std=c11 -o main.o main.c
fasm __switch_thread__.asm
flat assembler  version 1.73.22  (16384 kilobytes memory)
1 passes, 480 bytes.
gcc -o main main.o __switch_thread__.o
Hello from tid=1, got arg x=10
Hello from tid=2, got arg x=20
Hello from tid=3, got arg x=30
Hello from tid=4, got arg x=40
Hello from tid=1, got arg x=10
Hello from tid=2, got arg x=20
Hello from tid=3, got arg x=30
Hello from tid=4, got arg x=40
Hello from tid=1, got arg x=10
Hello from tid=2, got arg x=20
Hello from tid=3, got arg x=30
Hello from tid=4, got arg x=40
tid=1 result: 10
tid=2 result: 20
tid=3 result: 30
tid=4 result: 40
```
