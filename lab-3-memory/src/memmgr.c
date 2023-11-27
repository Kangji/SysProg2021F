//--------------------------------------------------------------------------------------------------
// System Programming                       Memory Lab                                   Fall 2021
//
/// @file
/// @brief dynamic memory manager
/// @author <Ji Ho Kang>
/// @studid <20XX-XXXXX>
//--------------------------------------------------------------------------------------------------


// Dynamic memory manager
// ======================
// This module implements a custom dynamic memory manager.
//
// Heap organization:
// ------------------
// The data segment for the heap is provided by the dataseg module. A 'word' in the heap is
// eight bytes.
//
// Implicit free list:
// -------------------
// - minimal block size: 32 bytes (header +footer + 2 data words)
// - h,f: header/footer of free block
// - H,F: header/footer of allocated block
//
// - state after initialization
//
//         initial sentinel half-block                  end sentinel half-block
//                   |                                             |
//   ds_heap_start   |   heap_start                         heap_end       ds_heap_brk
//               |   |   |                                         |       |
//               v   v   v                                         v       v
//               +---+---+-----------------------------------------+---+---+
//               |???| F | h :                                 : f | H |???|
//               +---+---+-----------------------------------------+---+---+
//                       ^                                         ^
//                       |                                         |
//               32-byte aligned                           32-byte aligned
//
// - allocation policies: first, next, best fit
// - block splitting: always at 32-byte boundaries
// - immediate coalescing upon free
//


#include <assert.h>
#include <error.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dataseg.h"
#include "memmgr.h"

void mm_check(void);

struct list_head {
  void *succ, *pred;
};

#define LIST_HEAD_INIT(name)  { &(name).succ, &(name).pred }

#define LIST_HEAD(name)                           \
  struct list_head name = LIST_HEAD_INIT(name)

/// @name global variables
/// @{
static void *ds_heap_start = NULL;                     ///< physical start of data segment
static void *ds_heap_brk   = NULL;                     ///< physical end of data segment
static void *heap_start    = NULL;                     ///< logical start of heap
static void *heap_end      = NULL;                     ///< logical end of heap
static int  PAGESIZE       = 0;                        ///< memory system page size
static void *(*get_free_block)(size_t) = NULL;         ///< get free block for selected allocation policy
static int  mm_initialized = 0;                        ///< initialized flag (yes: 1, otherwise 0)
static int  mm_loglevel    = 0;                        ///< log level (0: off; 1: info; 2: verbose)
static LIST_HEAD(__head);                              ///< address-ordered explicit free list head
static void *head = (void *)(&__head);                 ///< pointer to __head
static void *next_block = (void *)(&__head);              ///< iterator used in next-fit
/// @}

/// @name Macro definitions
/// @{
#define MIN(a, b)          ((a) < (b) ? (a) : (b))     ///< MIN function

#define TYPE               unsigned long               ///< word type of heap
#define TYPE_SIZE          sizeof(TYPE)                ///< size of word type

#define ALLOC              1                           ///< block allocated flag
#define FREE               0                           ///< block free flag
#define STATUS_MASK        ((TYPE)(0x7))               ///< mask to retrieve flagsfrom header/footer
#define SIZE_MASK          (~STATUS_MASK)              ///< mask to retrieve size from header/footer

#define CHUNKSIZE          (1*(1 << 12))               ///< size by which heap is extended
#define CHUNK_MASK         (~(CHUNKSIZE-1))            ///< chunk alignment mask

#define BS                 32                          ///< minimal block size. Must be a power of 2
#define BS_MASK            (~(BS-1))                   ///< block alignment mask

#define WORD(p)            ((TYPE)(p))                 ///< convert pointer to TYPE
#define PTR(w)             ((void*)(w))                ///< convert TYPE to void*

#define PREV_PTR(p)        ((p)-TYPE_SIZE)             ///< get pointer to word preceeding p
#define NEXT_PTR(p)        ((p)+TYPE_SIZE)             ///< get pointer to word succeeding p
#define JUMP(p, i)         ((p)+(i)*TYPE_SIZE)         ///< get pointer to word with offset i from p

#define PACK(size,status)  ((size) | (status))         ///< pack size & status into boundary tag
#define SENTINEL           (PACK(0, ALLOC))            ///< sentinel header/footer content
#define SIZE(v)            (v & SIZE_MASK)             ///< extract size from boundary tag
#define STATUS(v)          (v & STATUS_MASK)           ///< extract status from boundary tag

#define GET(p)             (*(TYPE*)(p))               ///< read word at *p
#define GET_SIZE(p)        (SIZE(GET(p)))              ///< extract size from header/footer
#define GET_STATUS(p)      (STATUS(GET(p)))            ///< extract status from header/footer
#define PUT(p, v)          (GET(p) = WORD(v))          ///< write word at *p

#define ALIGN(p)           (PTR(WORD(p) & BS_MASK))    ///< 32 byte alignment (round down) of pointer
#define CEIL_BS(v)         (((v)+BS-1) & BS_MASK)      ///< ceil up to the multiple of BS
#define CEIL_CS(v)         (((v)+CHUNKSIZE-1) & CHUNK_MASK) ///< ceil up to the multiple of CHUNKSIZE
#define FLOOR_CS(v)        ((v) & CHUNK_MASK)          ///< floor down to the multiple of CHUNKSIZE

#define NEXT_BLK(bp)       ((bp)+GET_SIZE(bp))         ///< get pointer to next block
#define PREV_BLK(bp)       ((bp)-GET_SIZE(PREV_PTR(bp)))    ///< get pointer to previous block

#define SUCC_PTR(bp)       (NEXT_PTR(bp))              ///< get pointer to successor pointer

#define NEXT_ENTRY(p)      (PTR(GET(p)))               ///< proceed one step in the linked list
#define PREV_ENTRY(succ)   (PREV_PTR(NEXT_ENTRY(NEXT_PTR(succ))))   ///< get successor pointer of previous free block

/// @brief iterate over a free list
/// @param pos pointer to use as a loop cursor
/// @param head of the free list
#define list_for_each(pos, head)                  \
  for (pos = NEXT_ENTRY(head); pos != (head); pos = NEXT_ENTRY(pos))

/// @brief print a log message if level <= mm_loglevel. The variadic argument is a printf format
///        string followed by its parametrs
#ifdef DEBUG
  #define LOG(level, ...) mm_log(level, __VA_ARGS__)

/// @brief print a log message. Do not call directly; use LOG() instead
/// @param level log level of message.
/// @param ... variadic parameters for vprintf function (format string with optional parameters)
static void mm_log(int level, ...)
{
  if (level > mm_loglevel) return;

  va_list va;
  va_start(va, level);
  const char *fmt = va_arg(va, const char*);

  if (fmt != NULL) vfprintf(stdout, fmt, va);

  va_end(va);

  fprintf(stdout, "\n");
}

#else
  #define LOG(level, ...)
#endif

/// @}


/// @name Program termination facilities
/// @{

/// @brief print error message and terminate process. The variadic argument is a printf format
///        string followed by its parameters
#define PANIC(...) mm_panic(__func__, __VA_ARGS__)

/// @brief print error message and terminate process. Do not call directly, Use PANIC() instead.
/// @param func function name
/// @param ... variadic parameters for vprintf function (format string with optional parameters)
static void mm_panic(const char *func, ...)
{
  va_list va;
  va_start(va, func);
  const char *fmt = va_arg(va, const char*);

  fprintf(stderr, "PANIC in %s%s", func, fmt ? ": " : ".");
  if (fmt != NULL) vfprintf(stderr, fmt, va);

  va_end(va);

  fprintf(stderr, "\n");

  exit(EXIT_FAILURE);
}
/// @}


static void* ff_get_free_block(size_t);
static void* nf_get_free_block(size_t);
static void* bf_get_free_block(size_t);

// auxillary functions
static inline int is_last_blk(void *);
static inline void validate(void *);
static inline void __list_add(void *, void *, void *);
static inline void list_add_before(void *, void *);
static void list_add(void *);
static inline void list_del(void *);
static void set_block(void *, void *, TYPE);
static void *coalesce(void *);
static void *extend_heap(size_t);
static void *optimized_free_blk(void *);

void mm_init(AllocationPolicy ap)
{
  LOG(1, "mm_init()");

  //
  // set allocation policy
  //
  char *apstr;
  switch (ap) {
    case ap_FirstFit: get_free_block = ff_get_free_block; apstr = "first fit"; break;
    case ap_NextFit:  get_free_block = nf_get_free_block; apstr = "next fit";  break;
    case ap_BestFit:  get_free_block = bf_get_free_block; apstr = "best fit";  break;
    default: PANIC("Invalid allocation policy.");
  }
  LOG(2, "  allocation policy       %s\n", apstr);

  //
  // retrieve heap status and perform a few initial sanity checks
  //
  ds_heap_stat(&ds_heap_start, &ds_heap_brk, NULL);
  PAGESIZE = ds_getpagesize();

  LOG(2, "  ds_heap_start:          %p\n"
         "  ds_heap_brk:            %p\n"
         "  PAGESIZE:               %d\n",
         ds_heap_start, ds_heap_brk, PAGESIZE);

  if (ds_heap_start == NULL) PANIC("Data segment not initialized.");
  if (ds_heap_start != ds_heap_brk) PANIC("Heap not clean.");
  if (PAGESIZE == 0) PANIC("Reported pagesize == 0.");

  //
  // initialize heap
  //
  if (ds_sbrk(CHUNKSIZE) == (void *)-1) PANIC("Not enough heap.");
  ds_heap_stat(&ds_heap_start, &ds_heap_brk, NULL);

  //
  // sentinel
  //
  heap_start = ALIGN(ds_heap_start + TYPE_SIZE - 1) + BS;
  heap_end = ALIGN(ds_heap_brk - TYPE_SIZE);

  PUT(PREV_PTR(heap_start), SENTINEL);
  PUT(heap_end, SENTINEL);

  //
  // initialize free block
  //
  set_block(heap_start, heap_end, FREE);
  if (heap_end > heap_start)  list_add_before(heap_start, head);

  //
  // heap is initialized
  //
  mm_initialized = 1;
}

void* mm_malloc(size_t size)
{
  LOG(1, "mm_malloc(0x%lx)", size);

  assert(mm_initialized);

  if (!size)  return NULL;

  void *target, *next_entry;
  size_t req_blk_size = CEIL_BS(size + 2 * TYPE_SIZE);

  //
  // find appropriate free block
  //
  if ( (target = get_free_block(req_blk_size)) ) {
    next_entry = NEXT_ENTRY(SUCC_PTR(target));

    list_del(target);
  }

  else {
    //
    // optimized extend_heap() - extend as little as possible
    //
    size_t last_free_blk_size = (GET_STATUS(PREV_PTR(heap_end)) == FREE) ? GET_SIZE(PREV_PTR(heap_end)) : 0;
    size_t chunk_size = CEIL_CS(req_blk_size - last_free_blk_size);

    if ( (target = extend_heap(chunk_size)) == (void *)-1 )  return NULL;  // errno = ENOMEM

    next_entry = head;
  }

  //
  // split target block
  //
  void *free_bp = target + req_blk_size, *next_bp = NEXT_BLK(target);

  set_block(target, free_bp, ALLOC);
  set_block(free_bp, next_bp, FREE);
  if (free_bp < next_bp)   list_add_before(free_bp, next_entry);

  return NEXT_PTR(target);
}

void* mm_calloc(size_t nmemb, size_t size)
{
  LOG(1, "mm_calloc(0x%lx, 0x%lx)", nmemb, size);

  assert(mm_initialized);

  //
  // calloc is simply malloc() followed by memset()
  //
  void *payload = mm_malloc(nmemb * size);

  if (payload != NULL) memset(payload, 0, nmemb * size);

  return payload;
}

void* mm_realloc(void *ptr, size_t size)
{
  LOG(1, "mm_realloc(%p, 0x%lx)", ptr, size);

  assert(mm_initialized);

  if (ptr)    validate(PREV_PTR(ptr));

  void *bp = ptr ? PREV_PTR(ptr) : NULL;
  size_t blk_size = ptr ? GET_SIZE(bp) : 0;
  size_t req_blk_size = size ? CEIL_BS(size + 2 * TYPE_SIZE) : 0;

  if (bp && GET_STATUS(bp) != ALLOC)
    PANIC("Invalid pointer %p.", ptr);

  if (!size || blk_size < req_blk_size) {
    void *new_ptr = mm_malloc(size);  // NULL if size == 0 or error

    if (ptr && new_ptr)     memcpy(new_ptr, ptr, MIN(blk_size - 2 * TYPE_SIZE, size));

    if (!size || new_ptr)   mm_free(ptr);   // Nop if ptr == NULL. Must not free if error occurred

    return new_ptr;
  }

  //
  // adjust block size
  //
  void *free_bp = bp + req_blk_size, *next_bp = NEXT_BLK(bp);

  set_block(bp, free_bp, ALLOC);
  set_block(free_bp, next_bp, FREE);

  free_bp = (free_bp < next_bp) ? optimized_free_blk(free_bp) : NULL;

  if (free_bp)  list_add(free_bp);

  return ptr;
}

void mm_free(void *ptr)
{
  LOG(1, "mm_free(%p)", ptr);

  assert(mm_initialized);

  if (!ptr)   return;

  //////////////////////////////////////////////////////////////////////////////////////////////////
  // validate bp.
  //
  // if status == ALLOC, cannot distinguish whether it is header of allocated block or it is just
  // a coincidence. So we just assume that it is a header, thus undefined behavior occurs if it is
  // not an appropriate pointer.
  //
  void *bp = PREV_PTR(ptr);

  validate(bp);

  if (GET_STATUS(bp) == FREE)
    PANIC("Double free detected on %p.", bp);

  else if (GET_STATUS(bp) != ALLOC)
    PANIC("Invalid pointer %p.", bp);

  //
  // coalesce and reduce heap if possible
  //
  bp = optimized_free_blk(bp);

  if (bp)   list_add(bp);
}

/// @name block allocation policites
/// @{

/// @brief find and return a free block of at least @a size bytes (first fit)
/// @param size size of block (including header & footer tags), in bytes
/// @retval void* pointer to header of large enough free block
/// @retval NULL if no free block of the requested size is avilable
static void* ff_get_free_block(size_t size)
{
  LOG(1, "ff_get_free_block(1x%lx (%lu))", size, size);

  assert(mm_initialized);

  //
  // start from the top of free list
  //
  void *pos = NULL;

  list_for_each(pos, head)  if (GET_SIZE(PREV_PTR(pos)) >= size)  break;

  return (pos != head) ? PREV_PTR(pos) : NULL;
}

/// @brief find and return a free block of at least @a size bytes (next fit)
/// @param size size of block (including header & footer tags), in bytes
/// @retval void* pointer to header of large enough free block
/// @retval NULL if no free block of the requested size is avilable
static void* nf_get_free_block(size_t size)
{
  LOG(1, "nf_get_free_block(0x%x (%lu))", size, size);

  assert(mm_initialized);

  //
  // start from the previous search end point
  //
  void *pos = NULL;

  list_for_each(pos, next_block)   if (pos != head && GET_SIZE(PREV_PTR(pos)) >= size)  break;

  if (pos != next_block || (pos != head && GET_SIZE(PREV_PTR(pos)) >= size)) {
    next_block = PREV_ENTRY(pos);

    return PREV_PTR(pos);
  }

  return NULL;
}

/// @brief find and return a free block of at least @a size bytes (best fit)
/// @param size size of block (including header & footer tags), in bytes
/// @retval void* pointer to header of large enough free block
/// @retval NULL if no free block of the requested size is avilable
static void* bf_get_free_block(size_t size)
{
  LOG(1, "bf_get_free_block(0x%lx (%lu))", size, size);

  assert(mm_initialized);

  //
  // examine every block
  //
  void *pos = NULL, *target = NULL;
  size_t target_size = heap_end - heap_start + 1;   // bigger than any possible block

  list_for_each(pos, head) {
    size_t pos_size = GET_SIZE(PREV_PTR(pos));

    if (pos_size >= size && pos_size < target_size) {
      target = PREV_PTR(pos);
      target_size = pos_size;
    }
  }

  return target;
}

/// @}

void mm_setloglevel(int level)
{
  mm_loglevel = level;
}


void mm_check(void)
{
  assert(mm_initialized);

  void *p;
  char *apstr;
  if (get_free_block == ff_get_free_block) apstr = "first fit";
  else if (get_free_block == nf_get_free_block) apstr = "next fit";
  else if (get_free_block == bf_get_free_block) apstr = "best fit";
  else apstr = "invalid";

  LOG(2, "  allocation policy    %s\n", apstr);
  printf("\n----------------------------------------- mm_check ----------------------------------------------\n");
  printf("  ds_heap_start:          %p\n", ds_heap_start);
  printf("  ds_heap_brk:            %p\n", ds_heap_brk);
  printf("  heap_start:             %p\n", heap_start);
  printf("  heap_end:               %p\n", heap_end);
  printf("  allocation policy:      %s\n", apstr);
  printf("  next_block:             %p\n", next_block);   // this will be needed for the next fit policy

  printf("\n");
  p = PREV_PTR(heap_start);
  printf("  initial sentinel:       %p: size: %6lx (%7ld), status: %s\n",
         p, GET_SIZE(p), GET_SIZE(p), GET_STATUS(p) == ALLOC ? "allocated" : "free");
  p = heap_end;
  printf("  end sentinel:           %p: size: %6lx (%7ld), status: %s\n",
         p, GET_SIZE(p), GET_SIZE(p), GET_STATUS(p) == ALLOC ? "allocated" : "free");
  printf("\n");
  printf("  blocks:\n");

  long errors = 0;
  p = heap_start;
  while (p < heap_end) {
    TYPE hdr = GET(p);
    TYPE size = SIZE(hdr);
    TYPE status = STATUS(hdr);
    printf("    %p: size: %6lx (%7ld), status: %s\n", 
           p, size, size, status == ALLOC ? "allocated" : "free");

    void *fp = p + size - TYPE_SIZE;
    TYPE ftr = GET(fp);
    TYPE fsize = SIZE(ftr);
    TYPE fstatus = STATUS(ftr);

    if ((size != fsize) || (status != fstatus)) {
      errors++;
      printf("    --> ERROR: footer at %p with different properties: size: %lx, status: %lx\n", 
             fp, fsize, fstatus);
    }

    p = p + size;
    if (size == 0) {
      printf("    WARNING: size 0 detected, aborting traversal.\n");
      break;
    }
  }

  printf("\n    Free List Traversal(Forward)\n");
  list_for_each(p, head) {
    TYPE hdr = GET(PREV_PTR(p));
    TYPE size = SIZE(hdr);
    printf("    %p: size: %6lx (%7ld)\n", PREV_PTR(p), size, size);
  }

  printf("\n    Free List Traversal(Backward)\n");
  list_for_each(p, NEXT_PTR(head)) {
    TYPE hdr = GET(PREV_PTR(PREV_PTR(p)));
    TYPE size = SIZE(hdr);
    printf("    %p: size: %6lx (%7ld)\n", PREV_PTR(PREV_PTR(p)), size, size);
  }

  printf("\n");
  if ((p == heap_end) && (errors == 0)) printf("  Block structure coherent.\n");
  printf("-------------------------------------------------------------------------------------------------\n");
}

/// @name auxillary functions
/// @{

/// @brief check if @a bp is the rightmost block in the heap
/// @param bp pointer to the block
/// @retval 1 if true
/// @retval 0 if false
static inline int is_last_blk(void *bp)
{
  return (NEXT_BLK(bp) == heap_end);
}

/// @brief validate @a ptr points inside the heap. Panic if not
/// @param ptr pointer to validate
static inline void validate(void *ptr)
{
  if (ptr < heap_start || ptr >= heap_end)  PANIC("Segmentation fault.");
}

/// @brief insert @a new between two known consecutive free blocks. This is only for internal
///        list manipulation where we know the @a prev, @a next free blocks already.
/// @param new pointer to the successor word of new free block to add
/// @param prev pointer to the successor word of previous free block
/// @param next pointer to the successor word of next free block
static inline void __list_add(void *new, void *prev, void *next)
{
  PUT(NEXT_PTR(next), NEXT_PTR(new));
  PUT(NEXT_PTR(new), NEXT_PTR(prev));
  PUT(new, next);
  PUT(prev, new);
}

/// @brief add @a bp to the address-ordered explicit free list, in front of @a next
/// @param bp pointer to block to add
/// @param next pointer to the successor word of next free block
static inline void list_add_before(void *bp, void *next)
{
  __list_add(SUCC_PTR(bp), PREV_ENTRY(next), next);
}

/// @brief add @a bp to the address-ordered explicit free list
/// @param bp pointer to block to add
static void list_add(void *bp)
{
  void *pos = NULL;

  // since we often modify the rightmost block(i.e., extend_heap()), leave it to special case
  if (is_last_blk(bp))  pos = head;

  else  list_for_each(pos, head)  if (pos > SUCC_PTR(bp))   break;

  list_add_before(bp, pos);
}

/// @brief delete @a bp from the address-ordered explicit free list
/// @param bp pointer to block to delete
static inline void list_del(void *bp)
{
  if (next_block == SUCC_PTR(bp))  next_block = PREV_ENTRY(next_block);
  PUT(PREV_ENTRY(SUCC_PTR(bp)), NEXT_ENTRY(SUCC_PTR(bp)));
  PUT(NEXT_PTR(NEXT_ENTRY(SUCC_PTR(bp))), NEXT_PTR(PREV_ENTRY(SUCC_PTR(bp))));
}

/// @brief set up header/footer of the block
/// @param start start of the block
/// @param end end of the block
static void set_block(void *start, void *end, TYPE alloc)
{
  TYPE content = PACK(SIZE(WORD(end - start)), alloc);

  if (content < BS) return;

  void *hdr = start, *ftr = PREV_PTR(end);

  PUT(hdr, content);
  PUT(ftr, content);
}

/// @brief coalesce free blocks. @a bp must not be in explicit free list.
/// @param bp main block to coalesce. It could be either allocated or free block, but the function
///        anyways consider the block as free block
/// @retval void *pointer to the coalesced free block
static void *coalesce(void *bp)
{
  void *prev = bp, *next = NEXT_BLK(bp);

  if (GET_STATUS(PREV_PTR(prev)) == FREE) {
    prev = PREV_BLK(prev);
    list_del(prev);
  }

  if (GET_STATUS(next) == FREE) {
    list_del(next);
    next = NEXT_BLK(next);
  }

  set_block(prev, next, FREE);

  return prev;
}

/// @brief extend heap to the @a size and coalesce
/// @param size size to extend
/// @retval void *pointer to the new free block
/// @retval -1 if failed to extend heap
static void *extend_heap(size_t size)
{
  void *old_heap_end = heap_end;

  if (ds_sbrk(size) == (void *)-1)   return (void *)-1;
  ds_heap_stat(&ds_heap_start, &ds_heap_brk, NULL);

  //
  // adjust heap_end
  //
  heap_end += size;
  PUT(heap_end, SENTINEL);
  set_block(old_heap_end, heap_end, FREE);

  return coalesce(old_heap_end);
}

/// @brief get optimized free block by coalescing and reducing heap if possible
/// @param bp pointer to the start of the block
/// @retval void *ptr to the optimized free block
/// @retval NULL if the block has disappeared due to optimization
static void *optimized_free_blk(void *bp)
{
  bp = coalesce(bp);

  if (is_last_blk(bp) && GET_SIZE(bp) >= CHUNKSIZE) {
    size_t chunk_size = FLOOR_CS(GET_SIZE(bp));

    if (ds_sbrk(-chunk_size) == (void *)-1)   return bp;
    ds_heap_stat(&ds_heap_start, &ds_heap_brk, NULL);

    //
    // adjust heap_end
    //
    heap_end -= chunk_size;
    PUT(heap_end, SENTINEL);
    set_block(bp, heap_end, FREE);
  }

  return (GET_STATUS(bp) == FREE) ? bp : NULL;
}

/// @}
