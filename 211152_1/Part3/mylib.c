#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#define BLOCK_SIZE 4194304 // 4MBs in bytes

void *fc_head = NULL; // Head of free memory linked list

unsigned long minFreeChunkSize(long unsigned size) {
  int retSize = ((size + 8 + 7) / 8) * 8;
  if (retSize < 24)
    retSize = 24;
  return retSize;
}

unsigned long fcSize(void *fc) { return *(unsigned long *)fc; }

void *nextNodeAddr(void *fc) { return (void *)(fc + 8); }

void *nextNode(void *fc) { return (void *)*(unsigned long *)(fc + 8); }

void *prevNodeAddr(void *fc) { return (void *)(fc + 16); }

void *prevNode(void *fc) { return (void *)*(unsigned long *)(fc + 16); }

void *freeChunkSearch(unsigned long size) {
  void *fc_ptr = fc_head;
  while (fc_ptr != NULL) {
    if (fcSize(fc_ptr) >=
        minFreeChunkSize(size)) { // Must include 8 byte offset
      return fc_ptr;
    }
    fc_ptr = nextNode(fc_ptr);
  }
  return NULL;
}

void addToListHead(void *fChunk) {
  if (fc_head == NULL) {
    fc_head = fChunk;
    return;
  }

  *(unsigned long *)nextNodeAddr(fChunk) = (unsigned long)
      fc_head; // Set next node address in fChunk to next node of head
  *(unsigned long *)prevNodeAddr(fc_head) =
      (unsigned long)fChunk; // Set prev node address of fc_chunk to fChunk

  fc_head = fChunk;
}

void removeFromList(void *fChunk) {
  if (fChunk == fc_head) {
    fc_head = nextNode(fChunk);
    if (fc_head != NULL)
      *(unsigned long *)prevNodeAddr(fc_head) = 0;
    return;
  }
  *(unsigned long *)nextNodeAddr(prevNode(fChunk)) =
      (unsigned long)nextNode(fChunk);
  if (nextNode(fChunk) != NULL)
    *(unsigned long *)prevNodeAddr(nextNode(fChunk)) =
        (unsigned long)prevNode(fChunk);
}

void *allocNewChunk(unsigned long size) { // Get more memory from the OS
  unsigned long requestSize =
      ((minFreeChunkSize(size) + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;

  void *memloc = mmap(NULL, requestSize, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  *(unsigned long *)memloc = requestSize;

  addToListHead(memloc);
  return memloc;
}

void *memalloc(unsigned long size) {
  if (size == 0) {
    return NULL;
  }

  // Search thorugh available free chunk to service request
  void *alloc_chunk = freeChunkSearch(size);
  if (alloc_chunk == NULL) {
    alloc_chunk = allocNewChunk(size);
  }

  if (fcSize(alloc_chunk) - minFreeChunkSize(size) >= 24) {
    void *remaining_chunk = alloc_chunk + minFreeChunkSize(size);
    *(unsigned long *)(remaining_chunk) =
        fcSize(alloc_chunk) - minFreeChunkSize(size);
    *(unsigned long *)(alloc_chunk) = minFreeChunkSize(size);
    addToListHead(remaining_chunk);
  }

  removeFromList(alloc_chunk);
  return alloc_chunk + 8;
}

void *locateLeftChunk(void *chunk_ptr) {
  void *fc_ptr = fc_head;
  while (fc_ptr != NULL) {
    if (fc_ptr + fcSize(fc_ptr) == chunk_ptr) {
      return fc_ptr;
    }
    fc_ptr = nextNode(fc_ptr);
  }
  return NULL;
}

void *locateRightChunk(void *chunk_ptr) {
  void *fc_ptr = fc_head;
  while (fc_ptr != NULL) {
    if (fc_ptr == chunk_ptr + fcSize(chunk_ptr)) {
      return fc_ptr;
    }
    fc_ptr = nextNode(fc_ptr);
  }
  return NULL;
}

void *combineChunks(void *leftChunk, void *chunk_ptr) {
  *(unsigned long *)(leftChunk) = fcSize(leftChunk) + fcSize(chunk_ptr);
  return leftChunk;
}

int memfree(void *ptr) {
  if (ptr == NULL) {
    return -1;
  }
  void *chunk_ptr = ptr - 8;

  void *leftChunk = locateLeftChunk(chunk_ptr);
  if (leftChunk != NULL) {
    removeFromList(leftChunk);
    chunk_ptr = combineChunks(leftChunk, chunk_ptr);
  }

  void *rightChunk = locateRightChunk(chunk_ptr);
  if (rightChunk != NULL) {
    removeFromList(rightChunk);
    chunk_ptr = combineChunks(chunk_ptr, rightChunk);
  }

  addToListHead(chunk_ptr);
  return 0;
}
