//
// CS252: MyMalloc Project
//
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include "MyMalloc.h"

static pthread_mutex_t mutex;

const int ArenaSize = 2097152;
const int NumberOfFreeLists = 1;

// Header of an object. Used both when the object is allocated and freed
struct ObjectHeader {
    size_t _objectSize;         // Real size of the object.
    int _allocated;             // 1 = yes, 0 = no 2 = sentinel
    struct ObjectHeader * _next;       // Points to the next object in the freelist (if free).
    struct ObjectHeader * _prev;       // Points to the previous object.
};

struct ObjectFooter {
    size_t _objectSize;
    int _allocated;
};

//The minimum size request
const size_t min_size = (8+sizeof(struct ObjectHeader)+sizeof(struct ObjectFooter));
//STATE of the allocator

// Size of the heap
static size_t _heapSize;

// initial memory pool
static void * _memStart;

// number of chunks request from OS
static int _numChunks;

// True if heap has been initialized
static int _initialized;

// Verbose mode
static int _verbose;

// # malloc calls
static int _mallocCalls;

// # free calls
static int _freeCalls;

// # realloc calls
static int _reallocCalls;

// # realloc calls
static int _callocCalls;

// Free list is a sentinel
static struct ObjectHeader _freeListSentinel; // Sentinel is used to simplify list operations
static struct ObjectHeader *_freeList;


//FUNCTIONS

//Initializes the heap
void initialize();

// Allocates an object 
void * allocateObject( size_t size );

// Frees an object
void freeObject( void * ptr );

// Returns the size of an object
size_t objectSize( void * ptr );

// At exit handler
void atExitHandler();

//Prints the heap size and other information about the allocator
void print();
void print_list();

// Gets memory from the OS
void * getMemoryFromOS( size_t size );

void increaseMallocCalls() { _mallocCalls++; }

void increaseReallocCalls() { _reallocCalls++; }

void increaseCallocCalls() { _callocCalls++; }

void increaseFreeCalls() { _freeCalls++; }

extern void
atExitHandlerInC()
{
	atExitHandler();
}

void initialize()
{
	// Environment var VERBOSE prints stats at end and turns on debugging
	// Default is on
	_verbose = 1;
	const char * envverbose = getenv( "MALLOCVERBOSE" );
	if ( envverbose && !strcmp( envverbose, "NO") ) {
	_verbose = 0;
	}

	pthread_mutex_init(&mutex, NULL);
	void * _mem = getMemoryFromOS( ArenaSize + (2*sizeof(struct ObjectHeader)) + (2*sizeof(struct ObjectFooter)) );

	// In verbose mode register also printing statistics at exit
	atexit( atExitHandlerInC );

	//establish fence posts
	struct ObjectFooter * fencepost1 = (struct ObjectFooter *)_mem;
	fencepost1->_allocated = 1;
	fencepost1->_objectSize = 123456789;
	char * temp = 
	  (char *)_mem + (2*sizeof(struct ObjectFooter)) + sizeof(struct ObjectHeader) + ArenaSize;
	struct ObjectHeader * fencepost2 = (struct ObjectHeader *)temp;
	fencepost2->_allocated = 1;
	fencepost2->_objectSize = 123456789;
	fencepost2->_next = NULL;
	fencepost2->_prev = NULL;

	//initialize the list to point to the _mem
	temp = (char *) _mem + sizeof(struct ObjectFooter);
	struct ObjectHeader * currentHeader = (struct ObjectHeader *) temp;
	temp = (char *)_mem + sizeof(struct ObjectFooter) + sizeof(struct ObjectHeader) + ArenaSize;
	struct ObjectFooter * currentFooter = (struct ObjectFooter *) temp;
	_freeList = &_freeListSentinel;
	currentHeader->_objectSize = ArenaSize + sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter); //2MB
	currentHeader->_allocated = 0;
	currentHeader->_next = _freeList;
	currentHeader->_prev = _freeList;
	currentFooter->_allocated = 0;
	currentFooter->_objectSize = currentHeader->_objectSize;
	_freeList->_prev = currentHeader;
	_freeList->_next = currentHeader; 
	_freeList->_allocated = 2; // sentinel. no coalescing.
	_freeList->_objectSize = 0;
	_memStart = (char*) currentHeader;
}

void * allocateObject( size_t size )
{
	//Make sure that allocator is initialized
	if ( !_initialized ) {
	_initialized = 1;
	initialize();
	}

	// Add the ObjectHeader/Footer to the size and round the total size up to a multiple of
	// 8 bytes for alignment.
	size_t roundedSize = (size + sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter) + 7) & ~7;

	struct ObjectHeader * _tempFirstChunk = _freeList->_next; //pointing to the first header
	
	//Checking if the list is empty. If yes, then allocating a new 2MB chink
	if(_tempFirstChunk == _freeList) {
			void * newMemory = getMemoryFromOS( ArenaSize + (2*sizeof(struct ObjectHeader)) + (2*sizeof(struct ObjectFooter)) );
			struct ObjectFooter * fencepost1 = (struct ObjectFooter *)newMemory;
			fencepost1->_allocated = 1;
			fencepost1->_objectSize = 123456789;
			char * temp = 
			(char *)newMemory + (2*sizeof(struct ObjectFooter)) + sizeof(struct ObjectHeader) + ArenaSize;
		  	struct ObjectHeader * fencepost2 = (struct ObjectHeader *)temp;
		  	fencepost2->_allocated = 1;
		  	fencepost2->_objectSize = 123456789;
		  	fencepost2->_next = NULL;
		  	fencepost2->_prev = NULL;

			temp = (char *) newMemory + sizeof(struct ObjectFooter);
			struct ObjectHeader * nextListHeader = (struct ObjectHeader *) temp;
			temp = (char *) newMemory + sizeof(struct ObjectFooter) + sizeof(struct ObjectHeader) + ArenaSize;
			struct ObjectFooter * nextListFooter = (struct ObjectFooter *) temp;

			nextListHeader->_objectSize = ArenaSize + sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter); //2MB
			nextListHeader->_allocated = 0;

			nextListFooter->_allocated = 0;
			nextListFooter->_objectSize = nextListHeader->_objectSize;
			
			//pointing the free list accordingly in the correct position
			_freeList->_next = nextListHeader;
			nextListHeader->_prev = _freeList;
			nextListHeader->_next = _freeList;
			_freeList->_prev = nextListHeader;
			
			//updating the looping variable
			_tempFirstChunk = nextListHeader;
	
	}
	while(_tempFirstChunk != _freeList) { // looping until the end
		if(_tempFirstChunk->_objectSize - roundedSize > min_size) { // if it qualifies to be split
			size_t block_size = _tempFirstChunk->_objectSize; // size of the block to be split

			void * marker = (void *)_tempFirstChunk; //marker for the block large enough
	
			/* Getting to the split point
			 * Getting the footer in place
			 * Marking the footer and original header to be allocated
			 * Object size change */
			char * temp = (char*)marker + roundedSize - sizeof(struct ObjectFooter); 
			struct ObjectFooter * newFooter = (struct ObjectFooter *)temp; 
			newFooter->_objectSize = roundedSize; 
			_tempFirstChunk->_objectSize = newFooter->_objectSize; 
			_tempFirstChunk->_allocated = 1;
			newFooter->_allocated = 1; 
			/* end */	
	
			/* Getting to the new header placement for the remaining
			 * New header's size and allocation settings */
			temp = (char *)marker + roundedSize; 
			struct ObjectHeader * newHeader = (struct ObjectHeader *) temp; 
			newHeader->_objectSize = (block_size - roundedSize); 
			newHeader->_allocated = 0;
			/* end */

	
			/* Getting to the original footer to change settings
			 * changing object size of this footer */
			temp = (char *)marker + (block_size - sizeof(struct ObjectFooter)); 
			struct ObjectFooter * prev_footer = (struct ObjectFooter *) temp; 
			prev_footer->_objectSize = newHeader->_objectSize; 
			prev_footer->_allocated = 0;
			/* end */

			//deleting the allocated pointer
			newHeader->_prev = _tempFirstChunk->_prev;
			(_tempFirstChunk->_prev)->_next = newHeader;

			newHeader->_next = _tempFirstChunk->_next;
			(_tempFirstChunk->_next)->_prev = newHeader;
			break;
	
		} else if((_tempFirstChunk->_objectSize - (roundedSize)) <= min_size && (_tempFirstChunk->_objectSize - (roundedSize)) >= 0) {
			size_t current_size = _tempFirstChunk->_objectSize;
			
			//deleting the allocated pointer
			(_tempFirstChunk->_prev)->_next = (_tempFirstChunk->_next);
			(_tempFirstChunk->_next)->_prev = (_tempFirstChunk->_prev);
	
			//marking it and updating it to be allocated
			_tempFirstChunk->_allocated = 1;
			void * marker = (void *) _tempFirstChunk;
			char * temp = (char *) marker + current_size - sizeof(struct ObjectFooter);
			struct ObjectFooter * currentFooter = (struct ObjectFooter *)temp;
			currentFooter->_allocated = 1;
			currentFooter->_objectSize = current_size;
			break;  

		} else {
			//if not found large enough
			_tempFirstChunk = _tempFirstChunk->_next;
			
			//if chunk is found large enough in the whole list, allocating a new 2 MB chunk			
			if(_tempFirstChunk == _freeList) {
				void * newMemory = getMemoryFromOS( ArenaSize + (2*sizeof(struct ObjectHeader)) + (2*sizeof(struct ObjectFooter)) );
				struct ObjectFooter * fencepost1 = (struct ObjectFooter *)newMemory;
				fencepost1->_allocated = 1;
				fencepost1->_objectSize = 123456789;
				char * temp = 
				(char *)newMemory + (2*sizeof(struct ObjectFooter)) + sizeof(struct ObjectHeader) + ArenaSize;
			  	struct ObjectHeader * fencepost2 = (struct ObjectHeader *)temp;
			  	fencepost2->_allocated = 1;
			  	fencepost2->_objectSize = 123456789;
			  	fencepost2->_next = NULL;
			  	fencepost2->_prev = NULL;

				temp = (char *) newMemory + sizeof(struct ObjectFooter);
				struct ObjectHeader * nextListHeader = (struct ObjectHeader *) temp;
				temp = (char *) newMemory + sizeof(struct ObjectFooter) + sizeof(struct ObjectHeader) + ArenaSize;
				struct ObjectFooter * nextListFooter = (struct ObjectFooter *) temp;

				nextListHeader->_objectSize = ArenaSize + sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter); //2MB
				nextListHeader->_allocated = 0;

				nextListFooter->_allocated = 0;
				nextListFooter->_objectSize = nextListHeader->_objectSize;
				
				//updating the freelist pointers according to the current and the new list
				(_tempFirstChunk->_prev)->_next = nextListHeader;
				nextListHeader->_prev = (_tempFirstChunk->_prev);
				nextListHeader->_next = _freeList;
				_freeList->_prev = nextListHeader;
				//updating the loop varaible
				_tempFirstChunk = nextListHeader;
	
			}
		}
	}
	pthread_mutex_unlock(&mutex);
	void * ret = (void *) _tempFirstChunk;
	char * temp = (char *) ret + sizeof(struct ObjectHeader);
	ret = (void *) temp;
	// Return a pointer to usable memory
	return (void *) (ret);

}

void freeObject( void * ptr )
{
	void * marker = ptr;
	char * temp = (char *) marker - sizeof(struct ObjectHeader);


	struct ObjectHeader * currentHeader = (struct ObjectHeader *) temp;
	size_t currentSize = currentHeader->_objectSize;


	temp = (char *) marker - sizeof(struct ObjectHeader) - sizeof(struct ObjectFooter) ;
	struct ObjectFooter * prevFooter = (struct ObjectFooter *) temp;


	temp = (char *) marker - sizeof(struct ObjectHeader) + currentSize;
	struct ObjectHeader * nextHeader = (struct ObjectHeader *) temp;

	//coalesce sequence
	//coalesce both sides  
	if(nextHeader->_allocated == 0 && prevFooter->_allocated == 0) {
		size_t prevSize = prevFooter->_objectSize;
		size_t nextSize = nextHeader->_objectSize;

		temp = (char *) marker - sizeof(struct ObjectHeader) - prevSize;
		struct ObjectHeader * prevHeader = (struct ObjectHeader *) temp;

		(prevHeader->_prev)->_next = (prevHeader->_next);
		(prevHeader->_next)->_prev = (prevHeader->_prev);

		(nextHeader->_prev)->_next = (nextHeader->_next);
		(nextHeader->_next)->_prev = (nextHeader->_prev);

		temp = (char *) marker + currentSize - sizeof(struct ObjectHeader) + nextSize - sizeof(struct ObjectFooter);
		struct ObjectFooter * nextFooter = (struct ObjectFooter *) temp;

		size_t newSize = prevSize + nextSize + currentSize;
		nextFooter->_objectSize = newSize;
		nextFooter->_allocated = 0;
		prevHeader->_objectSize = newSize;
		prevHeader->_allocated = 0;
		// finding the freed chunk's appropriate position
		struct ObjectHeader * tempStart = _freeList->_next;
		struct ObjectHeader * tempStart2 = _freeList;

		while(tempStart < prevHeader) {
			tempStart2 = tempStart;
			tempStart = tempStart->_next;
			if(tempStart == _freeList) {
				break;
			}
		}
		tempStart2->_next = prevHeader;
		prevHeader->_prev = tempStart2;
		prevHeader->_next = tempStart;
		tempStart->_prev = prevHeader;
	//coalesce right side
	} else if(nextHeader->_allocated == 0) {
		size_t nextSize = nextHeader->_objectSize;

		(nextHeader->_prev)->_next = (nextHeader->_next);
		(nextHeader->_next)->_prev = (nextHeader->_prev);

		currentHeader->_objectSize = currentSize + nextSize;
		temp = (char *) marker - sizeof(struct ObjectHeader) + currentSize + nextSize - sizeof(struct ObjectFooter);

		struct ObjectFooter * nextFooter = (struct ObjectFooter *) temp;
		nextFooter->_allocated = 0;
		nextFooter->_objectSize = currentHeader->_objectSize;
		currentHeader->_allocated = 0;

		// finding the freed chunk's appropriate position
		struct ObjectHeader * tempStart = _freeList->_next;
		struct ObjectHeader * tempStart2 = _freeList;

		while(tempStart < currentHeader) {
			tempStart2 = tempStart;
			tempStart = tempStart->_next;
			if(tempStart == _freeList) {
				break;
			}
		}
		tempStart2->_next = currentHeader;
		currentHeader->_prev = tempStart2;
		currentHeader->_next = tempStart;
		tempStart->_prev = currentHeader;
	//coalesce left side
	} else if(prevFooter->_allocated == 0) {
		size_t prevSize = prevFooter->_objectSize;
		temp = (char *) marker - sizeof(struct ObjectHeader) - prevSize;
		struct ObjectHeader * prevHeader = (struct ObjectHeader *) temp;

		(prevHeader->_prev)->_next = (prevHeader->_next);
		(prevHeader->_next)->_prev = (prevHeader->_prev);

		prevHeader->_objectSize = currentSize + prevSize;
		temp = (char *) marker - sizeof(struct ObjectHeader) + currentSize  - sizeof(struct ObjectFooter);
		struct ObjectFooter * currentFooter = (struct ObjectFooter *) temp;
		currentFooter->_objectSize = prevHeader->_objectSize;
		currentFooter->_allocated = 0;
		prevHeader->_allocated = 0;
		
		// finding the freed chunk's appropriate position
		struct ObjectHeader * tempStart = _freeList->_next;
		struct ObjectHeader * tempStart2 = _freeList;
		while(tempStart < prevHeader) {
			tempStart2 = tempStart;
			tempStart = tempStart->_next;
			if(tempStart == _freeList) {
				break;
			}
		}
		tempStart2->_next = prevHeader;
		prevHeader->_prev = tempStart2;
		prevHeader->_next = tempStart;
		tempStart->_prev = prevHeader;
	//no coalesce
	} else {
		temp = (char*) marker - sizeof(struct ObjectHeader) + currentSize - sizeof(struct ObjectFooter);
		struct ObjectFooter * currentFooter = (struct ObjectFooter *) temp;
		currentFooter->_allocated = 0;
		currentHeader->_allocated = 0;
		currentHeader->_objectSize = currentSize;
		currentFooter->_objectSize = currentSize;

		// finding the freed chunk's appropriate position
		struct ObjectHeader * tempStart = _freeList->_next;
		struct ObjectHeader * tempStart2 = _freeList;
		while(tempStart < currentHeader) {
			tempStart2 = tempStart;
			tempStart = tempStart->_next;
			if(tempStart == _freeList) {
				break;
			}
		}
		tempStart2->_next = currentHeader;
		currentHeader->_prev = tempStart2;
		currentHeader->_next = tempStart;
		tempStart->_prev = currentHeader;
	}
	return;
}

size_t objectSize( void * ptr )
{
  // Return the size of the object pointed by ptr. We assume that ptr is a valid obejct.
  struct ObjectHeader * o =
    (struct ObjectHeader *) ( (char *) ptr - sizeof(struct ObjectHeader) );

  // Substract the size of the header
  return o->_objectSize;
}

void print()
{
  printf("\n-------------------\n");

  printf("HeapSize:\t%zd bytes\n", _heapSize );
  printf("# mallocs:\t%d\n", _mallocCalls );
  printf("# reallocs:\t%d\n", _reallocCalls );
  printf("# callocs:\t%d\n", _callocCalls );
  printf("# frees:\t%d\n", _freeCalls );

  printf("\n-------------------\n");
}

void print_list()
{
  printf("FreeList: ");
  if ( !_initialized ) {
    _initialized = 1;
    initialize();
  }
  struct ObjectHeader * ptr = _freeList->_next;
  while(ptr != _freeList){
      long offset = (long)ptr - (long)_memStart;
      printf("[offset:%ld,size:%zd]",offset,ptr->_objectSize);
      ptr = ptr->_next;
      if(ptr != NULL){
          printf("->");
      }
  }
  printf("\n");
}

void * getMemoryFromOS( size_t size )
{
  // Use sbrk() to get memory from OS
  _heapSize += size;
 
  void * _mem = sbrk( size );

  if(!_initialized){
      _memStart = _mem;
  }

  _numChunks++;

  return _mem;
}

void atExitHandler()
{
  // Print statistics when exit
  if ( _verbose ) {
    print();
  }
}

//
// C interface
//

extern void *
malloc(size_t size)
{
  pthread_mutex_lock(&mutex);
  increaseMallocCalls();
  
  return allocateObject( size );
}

extern void
free(void *ptr)
{
  pthread_mutex_lock(&mutex);
  increaseFreeCalls();
  
  if ( ptr == 0 ) {
    // No object to free
    pthread_mutex_unlock(&mutex);
    return;
  }
  
  freeObject( ptr );
}

extern void *
realloc(void *ptr, size_t size)
{
  pthread_mutex_lock(&mutex);
  increaseReallocCalls();
    
  // Allocate new object
  void * newptr = allocateObject( size );

  // Copy old object only if ptr != 0
  if ( ptr != 0 ) {
    
    // copy only the minimum number of bytes
    size_t sizeToCopy =  objectSize( ptr );
    if ( sizeToCopy > size ) {
      sizeToCopy = size;
    }
    
    memcpy( newptr, ptr, sizeToCopy );

    //Free old object
    freeObject( ptr );
  }

  return newptr;
}

extern void *
calloc(size_t nelem, size_t elsize)
{
  pthread_mutex_lock(&mutex);
  increaseCallocCalls();
    
  // calloc allocates and initializes
  size_t size = nelem * elsize;

  void * ptr = allocateObject( size );

  if ( ptr ) {
    // No error
    // Initialize chunk with 0s
    memset( ptr, 0, size );
  }

  return ptr;
}


