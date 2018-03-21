# MyMalloc


#### Unit and structure of memory and management

Current version maintains memory as a doubly linked list.

**ObjectHeader**
- _objectSize: number of bits allocated in the chunk
- _allocated: if 0 its not occupied else (for instance, 1) it is.
- _next (ObjectHeader): next chunk     
- _prev (ObjectHeader): previous chunk

With a memory chunk of size ArenaSize (default 2097152 units of memory)

**ObjectFooter**
- _objectSize: number of bits allocated in the chunk
- _allocated: if 0 its not occupied else (for instance, 1) it is.

This data structure maintains chunks of contiguous free memory.
By default, it starts off with one list to manage the memory allocation and works it way up as memory demands increase.


---

The interface is similar to stdlib functions:
- malloc
- calloc
- realloc
- free

For detailed description: [here](http://www.cplusplus.com/reference/cstdlib/)

---

#### Future work
- The pool/arena allocation strategy has significant implications for the overall memory layout
