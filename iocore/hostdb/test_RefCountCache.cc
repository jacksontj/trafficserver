#define _test_RefCountCache_cc

#include <iostream>
#include "P_RefCountCache.h"
#include "I_EventSystem.h"
#include "ts/I_Layout.h"
#include "diags.i"


struct ExampleStruct {
  int idx;
  int name_offset;  // pointer addr to name

  // Return the char* to the name (TODO: cleaner interface??)
  char *name(){
    return (char *)this + this->name_offset;
  }
};

void fillCache(RefCountCache<ExampleStruct>* cache, int start, int end) {
  // TODO: name per?
  std::string name = "foobar";
  int allocSize = name.size() + 1;

  for (int i=start; i < end; i++) {
        ExampleStruct* tmp = (ExampleStruct*) cache->alloc((uint64_t) i, allocSize)->iobuf;

        tmp->idx = i;
        tmp->name_offset = sizeof(ExampleStruct);
        memcpy(tmp->name(), name.c_str(), name.size());
        // NULL terminate the string
        *(tmp->name() + name.size()) = '\0';

        // Print out the struct we put in there
        //printf("New ExampleStruct%d idx=%d name=%s allocSize=%d\n", i, tmp->idx, name.c_str(), allocSize);
    }
    printf("Loading complete! Cache now has %d items.\n\n", cache->numItems());
}

int verifyCache(RefCountCache<ExampleStruct>* cache, int start, int end) {
    // Re-query all the structs to make sure they are there and accurate
    for (int i=start; i < end; i++) {
		Ptr<RefCountCacheItem<ExampleStruct> > ccitem = cache->get(i);
		if (ccitem.m_ptr == NULL) {
			continue;
		}
        ExampleStruct* tmp = (ExampleStruct*) ccitem->iobuf;
        if (tmp == NULL) {
            //printf("ExampleStruct %d missing, skipping\n", i);
            continue;
        }
        //printf("Get (%p) ExampleStruct%d idx=%d name=%s\n", tmp, i, tmp->idx, tmp->name());

        // Check that idx is correct
        if (tmp->idx != i) {
            printf("IDX of ExampleStruct%d incorrect!\n", i);
            return 1;  // TODO: spin over all?
        }

        // check that the name is correct
        //if (strcmp(tmp->name, name.c_str())){
        //  printf("Name of ExampleStruct%d incorrect! %s %s\n", i, tmp->name, name.c_str());
        //  exit(1);
        //}
    }
    return 0;
}

int testRefcounting() {
	int ret = 0;

	printf("1\n");
	RefCountCache<ExampleStruct>* cache = new RefCountCache<ExampleStruct>(4);

	// Set an item in the cache
	ExampleStruct* tmp = (ExampleStruct*) cache->alloc((uint64_t) 1)->iobuf;
	printf("2\n");
	tmp->idx = 1;

	// Grab a pointer to item 1
	Ptr<RefCountCacheItem<ExampleStruct> > ccitem = cache->get((uint64_t) 1);

	ExampleStruct* tmpAfter = (ExampleStruct*) cache->get((uint64_t) 1)->iobuf;
	printf("3\n");
	printf("Item after %d %d\n", 1, tmpAfter->idx);

	// Delete a single item
	cache->del(1);
	printf("4\n");
	printf("Item after %d %d\n", 1, tmpAfter->idx);
	// verify that it still isn't in there
	ret |= cache->get(1) != NULL;
	printf("5\n");

	printf("Item after %d %d\n", 1, tmpAfter->idx);

	//RefCountCacheItem* item = new RefCountCacheItem(5, 5);

	//Ptr<RefCountCacheItem> itemPtr = make_ptr(item);


	return ret;
}


int main() {
  // Initialize IOBufAllocator
  RecModeT mode_type = RECM_STAND_ALONE;
  Layout::create();
  init_diags("", NULL);
  RecProcessInit(mode_type);
  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);

  //return testRefcounting();

    int ret = 0;

    printf("Starting tests\n");

    // Initialize our cache
    int cachePartitions = 4;
    RefCountCache<ExampleStruct>* cache = new RefCountCache<ExampleStruct>(cachePartitions);
    printf("Created...\n");

    cache->load_file("/tmp/hostdb_cache");
    printf("Cache started...\n");
    int numTestEntries = 10000;

    // See if anything persisted across the restart
    ret |= verifyCache(cache, 0, numTestEntries);
    printf("done verifying startup\n");

    // Clear the cache
    cache->clear();
    ret |= cache->numItems() != 0;

    // fill it
    printf("filling...\n");
    fillCache(cache, 0, numTestEntries);
    printf("filled...\n");

    // Verify that it has items
    printf("verifying...\n");
    ret |= verifyCache(cache, 0, numTestEntries);
    printf("verified...\n");

    // Verify that we can alloc() with no extra space
    printf("Alloc item idx 1\n");
    ExampleStruct* tmp = (ExampleStruct*) cache->alloc((uint64_t) 1)->iobuf;
    tmp->idx = 1;
    ExampleStruct* tmpAfter = (ExampleStruct*) cache->get((uint64_t) 1)->iobuf;
    printf("Item after %d %d\n", 1, tmpAfter->idx);
    // Verify every item in the cache
    ret |= verifyCache(cache, 0, numTestEntries);

    // Grab a pointer to item 1
	Ptr<RefCountCacheItem<ExampleStruct> > ccitem = cache->get((uint64_t) 1);
    // Delete a single item
    cache->del(1);
    // verify that it still isn't in there
    ret |= cache->get(1) != NULL;
    ret |= ((ExampleStruct *) ccitem->iobuf)->idx != 1;

    // Verify every item in the cache
    ret |= verifyCache(cache, 0, numTestEntries);

    // TODO: figure out how to test syncing/loading
    // write out the whole thing
    //printf("Sync return: %d\n", cache->sync_all());


    printf("TestRun: %d\n", ret);
    exit(ret);

    return ret;
}
