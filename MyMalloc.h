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