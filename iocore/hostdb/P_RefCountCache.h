#ifndef _P_RefCountCache_h_
#define _P_RefCountCache_h_

#include "I_EventSystem.h"

#include <cstdint>
#include <vector>
#include <unistd.h>
#include <string.h>
#include <unordered_map>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <cmath>
#include <fstream>

#include "ts/I_Version.h"


#define REFCOUNT_CACHE_EVENT_SYNC REFCOUNT_CACHE_EVENT_EVENTS_START

#define REFCOUNTCACHE_MAGIC_NUMBER 0x0BAD2D9
#define REFCOUNTCACHE_MAJOR_VERSION 1
#define REFCOUNTCACHE_MINOR_VERSION 0


// TODO: elsewhere?
#define PtrMutex Ptr<ProxyMutex>


// In the cache we want to recount our items
// This class will handle memory allocations (on the freelist) and
// take care of implementing RefCountObj-- so that the items we want in
// the cache don't have to inherit from it (which makes serialization/deserialization much easier)
class RefCountCacheItemBase: public RefCountObj {
public:
  RefCountCacheItemBase(uint64_t key=0, unsigned int size=0){
    this->key = key;
    this->size = size;

    if (this->size > 0) {
      // allocate a sufficiently large block from the ioBufAllocator
      this->iobuffer_index = iobuffer_size_to_index(size);
      ink_release_assert(this->iobuffer_index >= 0);
      this->iobuf = (char *)ioBufAllocator[this->iobuffer_index].alloc_void();
    }
  }
  ~RefCountCacheItemBase() {
  }
  void free() {
	  if (this->size > 0) {
		// free the block
		ioBufAllocator[iobuffer_index].free_void((void *)(iobuf));
	  }
  }
  uint64_t key;  // Key of this item
  unsigned int size;  // how much space does this guy get
  int64_t iobuffer_index;
  char *iobuf;
};

// Template to the particular class, so we can caste the item ptr correctly
template <class C> class RefCountCacheItem: public RefCountCacheItemBase {
public:
	using RefCountCacheItemBase::RefCountCacheItemBase;
	C* item();
};

template <class C> C* RefCountCacheItem<C>::item() {
	return (C *) this->iobuf;
}


// The RefCountCachePartition is simply a map of key -> Ptr<RefCountCacheItem<YourClass>>
// TODO: add metrics
// TODO: store partition number for debugging
template <class C> class RefCountCachePartition {
  public:
    RefCountCachePartition(int maxSize, int maxItems);
    RefCountCacheItem<C> *alloc(uint64_t key, int size=0);
    Ptr<RefCountCacheItem<C> > get(uint64_t key);
    void del(uint64_t key);
    void clear();

    int numItems();
    void copy(std::vector<Ptr<RefCountCacheItemBase>> *items);

    typedef typename std::unordered_map<uint64_t, Ptr<RefCountCacheItem<C>>>::iterator iteratortype;
    std::unordered_map<uint64_t, Ptr<RefCountCacheItem<C>>>* getMap();

    PtrMutex lock;  // Lock

  private:
    int maxSize;
    int maxItems;

    int size;
    int items;

    std::unordered_map<uint64_t, Ptr<RefCountCacheItem<C>>> itemMap;
};


template <class C> RefCountCachePartition<C>::RefCountCachePartition(int maxSize, int maxItems) {
  this->maxSize = maxSize;
  this->maxItems = maxItems;
  this->size = 0;
  this->items = 0;

  // Initialize lock
  this->lock = new_ProxyMutex();
}


// TODO: error somehow if you asked for something too big!
// Allocate space for an item `C` as well as `size` extra space.
template <class C> RefCountCacheItem<C>* RefCountCachePartition<C>::alloc(uint64_t key, int size) {
    size += sizeof(C);
    // Remove any colliding entries
    this->del(key);
    // TODO: check limits, evict if necessary

    RefCountCacheItem<C>* item = new RefCountCacheItem<C>(key, size);

    // Add our entry to the map
    this->itemMap[key] = make_ptr(item);

    return item;
}


template <class C> Ptr<RefCountCacheItem<C> > RefCountCachePartition<C>::get(uint64_t key) {
	RefCountCachePartition<C>::iteratortype i = this->itemMap.find(key);

    if (i == this->itemMap.end() ) {
      return Ptr<RefCountCacheItem<C> >(NULL);
    } else {
      // found
      return make_ptr(i->second.m_ptr);
    }
}


template <class C> void RefCountCachePartition<C>::del(uint64_t key) {
  RefCountCachePartition<C>::iteratortype i = this->itemMap.find(key);

  if (i != this->itemMap.end() ) {
    // found, lets remove it
    this->itemMap.erase(i);
    // Remove our refcount by deleting it
    delete i->second;
  }
}


template <class C> void RefCountCachePartition<C>::clear() {
  // Clear the in memory hashmap
  // TODO: delete all items (not sure if clear calls delete on all items)
  this->itemMap.clear();
  this->items = 0;
  this->size = 0;
}


template <class C> int RefCountCachePartition<C>::numItems() {
  return this->itemMap.size();
}


template <class C> void RefCountCachePartition<C>::copy(std::vector<Ptr<RefCountCacheItemBase>> *items){
  for (RefCountCachePartition<C>::iteratortype i = this->itemMap.begin(); i != this->itemMap.end(); ++i) {
    items->push_back(make_ptr((RefCountCacheItemBase *) i->second));
  }
}

// TODO: pass an iterator or something? Seems not good to pass a pointer to a member
template <class C> std::unordered_map<uint64_t, Ptr<RefCountCacheItem<C>>>* RefCountCachePartition<C>::getMap(){
    return &this->itemMap;
}



// The implementation of this class must be in here, as this is used by template
// classes, and c++ rocks that way
// The header for the cache, this is used to check if the serialized cache is compatible
class RefCountCacheHeader {
  public:
    unsigned int magic;
    VersionNumber version;

    int numPartitions;
    int partitionMaxSize;


    RefCountCacheHeader(int numParts, int partSize): magic(REFCOUNTCACHE_MAGIC_NUMBER), numPartitions(numParts), partitionMaxSize(partSize){
      this->version.ink_major = REFCOUNTCACHE_MAJOR_VERSION;
      this->version.ink_minor = REFCOUNTCACHE_MINOR_VERSION;
    };

    bool operator == (const RefCountCacheHeader other) const {
      return (
        this->magic == other.magic &&
        this->version.ink_major == other.version.ink_major &&
        this->version.ink_minor == other.version.ink_minor &&
        this->numPartitions == other.numPartitions &&
        this->partitionMaxSize == other.partitionMaxSize
      );
    }

    bool compatible(RefCountCacheHeader *other){
//      printf("magic %d %d\n", this->magic, other->magic);
//      printf("version.ink_major %d %d\n", this->version.ink_major, other->version.ink_major);
//      printf("numPartitions %d %d\n", this->numPartitions, other->numPartitions);
//      printf("partitionMaxSize %d %d\n", this->partitionMaxSize, other->partitionMaxSize);

      return (
        this->magic == other->magic &&
        this->version.ink_major == other->version.ink_major &&
        this->numPartitions == other->numPartitions &&
        this->partitionMaxSize == other->partitionMaxSize
      );
    };
};


// Base for syncing purposes
// Since the actual RefCountCache is templated, we don't want to require
// importing the template type for all uses (such as syncing)
class RefCountCacheBase {
  public:
    void sync_partitions(Continuation *cont);
    virtual RefCountCacheHeader *get_header() {return NULL;};
    virtual ProxyMutex* lock_for_partition(int){return NULL;};
    virtual int partition_count() {return 0;};
    virtual int partition_itemcount(int part){return 0;};
    virtual void copy_partition(int part, std::vector<Ptr<RefCountCacheItemBase>> *items) {};
    virtual std::string get_filepath() {return NULL;};
};


// RefCountCache is a ref-counted key->value map to store "atomic" blocks of memory (from the freelist)
// which includes at least an instance of `C`. The primary use case is that the struct (`C`)
// has a dynamically sized attribute (array, string, etc.). To accommodate this use-case
// RefCountCache allows you to call alloc() with a key and an optional `size` which will be the
// additional amount of space required to store your additional information.
// Ptrs to these "blocks" of memory can be retrieved again using the get() method with the key provided
// during the alloc()
template <class C> class RefCountCache: public RefCountCacheBase {
    public:
        // Constructor
        RefCountCache(int numPartitions, int size=-1, int items=-1);
        // Destructor
        ~RefCountCache();
        RefCountCacheItem<C> *alloc(uint64_t key, int size=0);
        Ptr<RefCountCacheItem<C>> get(uint64_t key);
        int partitionForKey(uint64_t key);
        void del(uint64_t key);
        void clear();

        RefCountCacheHeader *get_header();
        ProxyMutex* lock_for_key(uint64_t key);
        virtual ProxyMutex* lock_for_partition(int pnum);
        int partition_count();
        int partition_itemcount(int part);
        std::unordered_map<uint64_t, Ptr<RefCountCacheItem<C>>>* partition_getMap(int part);
        void copy_partition(int part, std::vector<Ptr<RefCountCacheItemBase>> *items);
        std::string get_filepath();

        // TODO: better names
        int numItems();

        int load_file(std::string filepath);  // Load the file from disk (if exists)
        int totalSize();

        // Sync methods
        int sync_all();

    private:
        int maxSize; // Total size
        int maxItems; // Total number of items allowed

        int numPartitions;
        int partitionMaxSize;
        int partitionMaxItems;

        std::string filepath;

        std::vector<RefCountCachePartition<C>> partitions;

        // Header
        RefCountCacheHeader header;  // Our header
};


// TODO: add `calloc`
template <class C> RefCountCache<C>::RefCountCache(int numPartitions, int size, int items):
    partitions(numPartitions, RefCountCachePartition<C>(size/numPartitions, items/numPartitions)), header(RefCountCacheHeader(numPartitions, size)){
    this->maxSize = size;
    this->maxItems = items;
    this->numPartitions = numPartitions;

    // TODO: fix if either is not divisible by numPartitions
    this->partitionMaxSize = this->maxSize / this->numPartitions;
    this->partitionMaxItems = this->maxItems / this->numPartitions;

    // TODO: fix this, bad to initialize it wrong
    this->header.partitionMaxSize = this->partitionMaxSize;
}


// Deconstruct the class
template <class C> RefCountCache<C>::~RefCountCache() {
  delete this->partitions;
}


template <class C> RefCountCacheItem<C>* RefCountCache<C>::alloc(uint64_t key, int size) {
    return this->partitions[this->partitionForKey(key)].alloc(key, size);
}


template <class C> Ptr<RefCountCacheItem<C>> RefCountCache<C>::get(uint64_t key) {
    return this->partitions[this->partitionForKey(key)].get(key);
}

// Pick a partition for a given item
template <class C> int RefCountCache<C>::partitionForKey(uint64_t key) {
    return key % this->numPartitions;
}


// TODO: return a copy???
template <class C> RefCountCacheHeader* RefCountCache<C>::get_header() {
  return &this->header;
}


template <class C> ProxyMutex* RefCountCache<C>::lock_for_key(uint64_t key) {
  return this->partitions[this->partitionForKey(key)].lock;
}


template <class C> ProxyMutex* RefCountCache<C>::lock_for_partition(int pnum) {
  return this->partitions[pnum].lock;
}

template <class C> int RefCountCache<C>::partition_count() {
  return this->numPartitions;
}

template <class C> int RefCountCache<C>::partition_itemcount(int pnum) {
  return this->partitions[pnum].numItems();
}

template <class C> std::unordered_map<uint64_t, Ptr<RefCountCacheItem<C>>>* RefCountCache<C>::partition_getMap(int pnum) {
  return this->partitions[pnum].getMap();
}

template <class C> void RefCountCache<C>::copy_partition(int pnum, std::vector<Ptr<RefCountCacheItemBase>> *items) {
  this->partitions[pnum].copy(items);
}

template <class C> std::string RefCountCache<C>::get_filepath(){
  return this->filepath;
}

template <class C> void RefCountCache<C>::del(uint64_t key) {
  this->partitions[this->partitionForKey(key)].del(key);
}


template <class C> void RefCountCache<C>::clear() {
  for (int i = 0; i < this->numPartitions; i++) {
    this->partitions[i].clear();
  }
}

// Return the number of items in all partitions of this cache
template <class C> int RefCountCache<C>::numItems() {
    int ret = 0;
    for (int i = 0; i < this->numPartitions; i++) {
        ret += this->partitions[i].numItems();
    }
    return ret;
}


// Attempt to open a file at `filepath`. If we can open the file, we'll attempt to
// load items from it (assuming the header is compatible). Afterwards we'll keep
// the FD open and use this as the backing store for our cache
// The return is the number of items loaded from disk. Errors are -1
// TODO: error if you already have a filepath
template <class C> int RefCountCache<C>::load_file(std::string filepath) {
  this->filepath = filepath;
  std::ifstream infile;
  infile.open (this->filepath.c_str(), std::ios::in|std::ios::binary);

  // read in the header
  RefCountCacheHeader tmpHeader = RefCountCacheHeader(0, 0);
  // TODO: check if we read it in
  infile.read((char *) &tmpHeader, sizeof(RefCountCacheHeader));
  if (!this->header.compatible(&tmpHeader)){

	  Warning("Incompatible cache at %s, not loading.", filepath.c_str());
	  return -1;  // TODO: specific code for incompatible
  }

  RefCountCacheItemBase tmpItem = RefCountCacheItemBase();
  while (infile.read((char *) &tmpItem, sizeof(tmpItem))) {
    C *newItem = (C*) this->alloc(tmpItem.key, tmpItem.size - sizeof(C))->iobuf;
    infile.read((char *) newItem, tmpItem.size);
  }
  // TODO: remove this? this is a hack to not have the destructor try to deallocate the iobuff
  tmpItem.size = 0;
  infile.close();

  // TODO: catch error conditions!
  return 0;
}



class RefCountCacheSync;
typedef int (RefCountCacheSync::*MCacheSyncHandler)(int, void *);
// This continuation is responsible for persisting RefCountCache to disk
// To avoid locking the partitions for a long time we'll do the following per-partition:
//    - lock
//    - copy ptrs
//    - unlock
//    - persist
// This way we only have to hold the lock on the partition for the time it takes to get Ptrs to all items in the partition
class RefCountCacheSync: public Continuation {
public:
  int partition; // Current partition
  RefCountCacheBase *cc; // Pointer to the entire cache
  Continuation *cont;

  std::vector<Ptr<RefCountCacheItemBase>> partitionItems;

  int copyPartition(int event, Event *e);
  int writePartition(int event, Event *e);
  int pauseEvent(int event, Event *e);

  // Create the tmp file on disk we'll be writing to
  int initializeStorage(int event, Event *e);
  // do the final mv and close of file handle
  int finalizeSync();

  RefCountCacheSync(Continuation *acont, RefCountCacheBase *cc);

private:
  std::ofstream outfile;

  std::string filename;
  std::string tmp_filename;

};


#endif /* _P_RefCountCache_h_ */
