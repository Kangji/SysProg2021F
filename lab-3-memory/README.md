# Lab 3: Memory Lab

In this lab, we implement our own dynamic memory manager.

You will learn
   * how a dynamic memory manager works
   * how to implement a dynamic memory manager
   * how to work with macros in C
   * how to work with function pointers in C
   * how to debug code
   * that writing a dynamic memory manager is simple in theory and difficult in practice.

[[_TOC_]]


## Important Dates

| Date | Description |
|:---  |:--- |
| Wednesday, October 6, 18:30 | Memory Lab hand-out |
| Wednesday, October 13, 18:30 | Memory Lab session 1 |
| Wednesday, October 20, 18:30 | Memory Lab session 2 |
| Sunday, October 31, 17:00 | Submission deadline (extended) |

There are no separate deadlines for phases 1 and 2. At the end of the lab, both phases should be implemented.

## Logistics

### Hand-out

Start by forking the lab into your namespace and making sure the lab visibility is set to private. Read the instructions here carefully. Then clone the lab to your local computer and get to work. 

### Submission

Commit and push your work frequently to avoid data loss. Once you are happy with your solution and want to submit it for grading, create a tag called "Submission". The timestamp of the "Submission" tag (which is attached you your last commit and thus has the identical same timestamp) counts as your submission time.

To create a tag, go to your repository on https://teaching.csap.snu.ac.kr/ and navigate to Repository -> Tags. Enter "Submission" as the Tag name then hit "Create tag". You can leave the other fields empty.  

If you later discover an error and want to update your submission, you can delete the "Submission" tag, fix your solution, and create a new "Submission" tag. If that happens _after_ the submission deadline, you also need to email the TAs so that they are aware of the update to your submission.


## Dynamic Memory Manager

### Overview

The dynamic memory manager implemented in this lab provides the same API as the C standard library's memory manager: `malloc()`, `free()`, `calloc()`, and `realloc()`. In addition to these four functions, additional methods for initialization, logging, and debugging need to be implemented.
The following table shows the API of our dynamic memory manager:

| API function | libc | Description |
|:---          |:---  |:---         |
| `void* mm_malloc(size_t size)`| `malloc`  | allocate a block of memory with a payload size of (at least) _size_ bytes |
| `void mm_free(void *ptr)` | `free` | free a previously allocated block of memory |
| `void* mm_calloc(size_t nelem, size_t size)` | `calloc` | allocate a block of memory with a payload size of (at least) _size_ bytes and initialize with zeroes |
| `void* mm_realloc(void *ptr, size_t size)` | `realloc` | change the size of a previously allocated block _ptr_ to a new _size_. This operation may need to move the memory block to a different location. The original payload is preserved up to _max(old size, new size)_ |
| `void mm_init(void)`  | n/a  | initialize dynamic memory manager |
| `void mm_setloglevel(int level)` | similar to `mtrace()` | set the logging level of the allocator |
| `void mm_check(void)` | simiar to `mcheck()` | check and dump the status of the heap |


### Operation

Since libc's memory allocator is built into the standard C runtime, our memory manager cannot directly manipulate the heap of the process. Instead, we operate on a simulated heap. The simulated heap is controlled with the same two functions the kernel offers for that purpose: `sbrk()` and `getpagesize()`.

The C runtime initializes the heap and the dynamic memory manager automatically whenever a process is started. For our memory manager, we have to do that manually, hence the `mm_init()` function. Similarly, the simulated heap has to be initialized before it can be used by calling `ds_allocate()`. 

The following diagram shows the organization and operation of our allocator:

```
                                      File: mm_test.c
  +-------------------------------------------------+
  | user-level process. After initializing the data |
  | segment and the heap, mm_malloc/free/calloc/    |
  | realloc can be used as in libc.                 |
  +-------------------------------------------------+
     |            |                    |
1. ds_allocate()  |       3. sequence of mm_malloc(),
     |            |           mm_free(), mm_calloc(),
     |       2. mm_init()        and mm_realloc()
     |            |                    |
     |            v                    v
     |    +-----------------------------------------+
     |    | custom memory manager. Manages the heap |
     |    +-----------------------------------------+
     |    File: memmgr.c        |
     |                       ds_sbrk()
     |                          |
     v                          v
  +-------------------------------------------------+
  | custom data segment implementation. Manages the |
  | data segment to be used by our allocator.       |
  +-------------------------------------------------+
  File: dataseg.c
 ```                                        

### Heap Organization

Our memory manager is a 64-bit operator, i.e., one word in the allocator is 8 bytes long. The heap is conceptually organized into blocks. The minimal block size is 32 bytes. Each block must have a boundary tag (header/footer). Free list management is not specified; you can implement an implicit or an explicit free list.

The boundary tags comprise of the size of the block and an allocated bit. Since block sizes are a muliple of 32, the low 4 bits of the size are always 0. We use bit 0 to indicate the status of the block (1: allocated, 0: free).

You are free to add special sentinel blocks at the start and end of the heap to simplify the operation of the allocator.


### API Specification

#### mm_init()

You can assume that `mm_init()` is called before any other operations on the heap are performed. The function needs to initialize the heap to its initial state.

### mm_malloc()

The `void* mm_malloc(size_t size)` routine returns a pointer to an allocated payload block of at least
_size_ bytes. The entire allocated block must lie within the heap region and must not overlap with
any other block.

### mm_calloc()

`void* mm_calloc(size_t nelem, size_t size)` returns a pointer to an allocated payload block of at least
_nelem*size_ bytes that is initialized to zero. The same constraints as for `mm_malloc()` apply.

### mm_realloc()

The `void* mm_realloc(void *ptr, size_t size)` routine returns a pointer to an allocated region of at least size
bytes with the following constraints.

* if `ptr` is NULL, the call is equivalent to `mm_malloc(size)`
* if `size` is equal to zero, the call is equivalent to `mm_free(ptr)`
* if `ptr` is not NULL, it must point to a valid allocated block. The call to `mm_realloc` changes
the size of the memory block pointed to by `ptr` (the old block) to `size` bytes and returns the
address of the new block. Notice that the address of the new block might be the same as the
old block, or it might be different, depending on your implementation, the amount of internal
fragmentation in the old block, and the size of the `realloc` request.
The contents of the new block are the same as those of the old `ptr` block, up to the minimum of
the old and new sizes. Everything else is uninitialized. For example, if the old block is 8 bytes
and the new block is 12 bytes, then the first 8 bytes of the new block are identical to the first 8
bytes of the old block and the last 4 bytes are uninitialized. 

### mm_free()

The `void mm_free(void *ptr)` routine frees the block pointed to by `ptr` that was returned by an earlier call to
`mm_malloc()`, `mm_calloc()`, or `mm_realloc()` and has not yet been freed. When when the callee tries to free a freed
memory block, an error is printed.


### Free block management and policies

Free list management is not specified; you can implement an implicit or an explicit free list.

You will implement three types of allocation policies: first fit, next fit, and best fit.

* **First fit**: Searches the dree list from the beginning and chooses the first free block that fits.
* **Best fit**: Examines every free block and chooses the smallest free block that is fits.
* **Next fit**: Similar to first, but instead of starting each search at the beginning of the list, it 
continues the search where the precious allocation left off.



## Handout Overview

The handout contains the following files and directories

| File/Directory | Description |
|:---  |:--- |
| doc/ | Doxygen instructions, configuration file, and auto-generated documentation |
| driver/ | Pre-compiled modules required to link your implementation to the `mm_driver` test program |
| reference/ | Reference implementation |
| src/ | Source code of the lab. You will modify memmgr.c/h, mm_test.c, and may add new files |
| tests/ | Test allocation/deallocation sequences to test your allocator |
| README.md | this file |
| Makefile | Makefile driver program |
| .gitignore | Tells git which files to ignore |

The files you will work on or modify are all located in the `src/` directory

| File | Description |
|:---  |:--- |
| src/blocklist.c/h | Implementation of a list to manage allocated blocks for debugging/verification purposes. Do not modify! |
| src/datasec.c/h | Implementation of the data segment. Do not modify! |
| src/nulldriver.c/h | Implementation of an empty allocator that does nothing. Useful to measure overhead. Do not modify! |
| src/memmgr.c/h | The dynamic memory manager. A skeletton is provided. Implement your solution by editing the C file. |
| src/mm_test.c  | A simple test program to test your implementation step-by-step. |

### Reference implementation

The directory `reference` contains a simple test driver program. You can use it to understand how our allocator works but should not take the output literally.


## Phase 1

Your task in phase 1 is to implement the basic functionality of the dynamic memory allocator: `malloc()` and `free()`. Also implement `calloc()` which is basically `malloc()` followed by an initialization with 0.

### Design

In a first step, write down the logical steps of your program on a sheet of paper. We will do that together during the first lab session.

### Implementation

Once you have designed the outline of your implementation, you can start implementing it. We provide a skeleton file to help you get started.

The skeleton provides some global variables and macros that we think will be helpful,  logging and panic functions, and skeletons for the individual `mm_X()` functions that are more or less complete.

Start by working on `mm_init()`. Use the `mm_check()` function to inspect your heap. Once the initial heap looks good, proceed with the implementation of `mm_malloc()`, followed by `mm_free()`.

Have a look at `mm_test.c` and modify the code in there to test different cases. Have a look at the documentation of `mm_setloglevel()`, `ds_setloglevel()`, and `mm_check()`; these functions will be very handy to understand what's going on and debug your code.


## Phase 2

In phase 2, implement the next fit and best fit allocation policies and finalize your allocator. Implementing `mm_realloc()` is recommended, but not absolutely necessary (i.e., only few points are awared for realloc in the evaluation).
You can now test your allocator with the test driver (`make mm_driver`, see below) and compare it to the reference implementation.


## Evaluation
For the evaluation, we use a driver program that issues a series of memory allocation and deallocation requests to your implementation. The grading focuses on implementation completeness and correctness over performance.

### Grading
We expect you to implement `mm_malloc()`, `mm_free()`, and `mm_calloc()`. `mm_realloc()` is not mandatory but will be awarded a (few) points when implemented.
We will test the correctness of your implementation and measure the heap utilization. Performance is also considered, but is much less important than correctness and heap utilization.

Your code needs to be documented (Doxygen for the interface, and additional regular comments inside files/functions where you deem necessary).
 
### mm_driver
The handout includes a test version of `mm_driver` in pre-compiled form in the `obj/` directory. You can build the driver and link it to your memory manager by running
```bash
$ make mm_driver
gcc -Wall -Wno-stringop-truncation -O2 -g -MMD -MP -o memmgr.o -c memmgr.c
gcc -Wall -Wno-stringop-truncation -O2 -g -o mm_driver memmgr.o dataseg.o obj/blocklist.o obj/mm_driver.o
```

The `mm_driver` program takes as an input a script file that describes the test. We provide two simple tests in the directory `tests/`. The scripts should be self-explanatory.
Use as follows:
```bash
$ ./mm_driver tests/demo.dmas
mm_malloc(0x10 (16))
bf_get_next_block(0x20 (32))
mm_malloc(0x32 (50))
bf_get_next_block(0x60 (96))
...
--------------------------------------------
Statistics:
  actions:              17
    malloc:              9
    calloc:              0
    realloc:             0
    free:                8
  time:             0.000475910 sec
  
  performance:      35.72 kops/sec
--------------------------------------------
```

## Hints

### Skeleton code
The skeleton code is meant to help you get started. You can modify it in any way you see fit - or implement this lab completely from scratch.

### Allocator in the textbook
The textbook contains an implementation of a simple allocator with a free list in chapter 9.9.12. 
It is quite similar to what you need to do here. Try by yourself first. If you are stuck, study the allocator from the book and apply your gained knowledge to your code.

### Final words

Implementing a dynamic memory allocator is easy in theory and very difficult in practice. A tiny mistake may destroy your heap, and to make matters worse, you may not notice that error until a few `mm_malloc()` and `mm_free()` operations later. At the beginning, you may feel overwhelmed and have no idea how to approach this task. 

Do not despair - we will give detailed instructions during the lab sessions and provide individual help so that each of you can finish this lab. After completing this lab, you will understand dynamic memory allocation from both the user and the system perspective.

<div align="center" style="font-size: 1.75em;">

**Happy coding!**
</div>
