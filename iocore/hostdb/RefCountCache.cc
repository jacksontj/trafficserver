// The goal here is to create a proof-of-concept cache struct which will cache an item
// for at least N seconds (configurable) and alloc a given amount of space for the given
// struct-- this way all of the dynamic pieces (strings, arrays, etc.) can be allocated
// within the same block.

#include "ts/ink_platform.h"
#include "ts/I_Layout.h"
#include "P_HostDB.h"
#include "P_RefCountCache.h"
#include "P_EventSystem.h" // FIXME: need to have this in I_* header files.
#include "ts/ink_file.h"


#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <unordered_map>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <cmath>


// TODO: remove? Config?
//static const int MC_SYNC_MIN_PAUSE_TIME = HRTIME_MSECONDS(200); // Pause for at least 200ms


void RefCountCacheBase::sync_partitions(Continuation *cont) {
  eventProcessor.schedule_imm(new RefCountCacheSync(cont, this), ET_CALL);
}


int RefCountCacheSync::copyPartition(int event, Event *e) {
    (void) event;
    if (partition >= cc->partition_count()) {
      // TODO: check return
      this->finalizeSync();
      cont->handleEvent(REFCOUNT_CACHE_EVENT_SYNC, 0);
      Debug("multicache", "RefCountCacheSync done");
      delete this;
      return EVENT_DONE;
    }
    Debug("hostdb", "sync partition=%d/%d", partition, cc->partition_count());
    // copy the partition into our buffer, then we'll let `pauseEvent` write it out
    this->partitionItems.reserve(cc->partition_itemcount(partition));
    cc->copy_partition(partition, &this->partitionItems);
    partition++;
    SET_HANDLER((MCacheSyncHandler) &RefCountCacheSync::writePartition);
    mutex = e->ethread->mutex;
    e->schedule_imm();

    return EVENT_CONT;
  }

int RefCountCacheSync::writePartition(int event, Event *e) {
  // write the partition to disk
  // for item in this->partitionItems
  // write to disk with headers per item
  for(std::vector<Ptr<RefCountCacheItemBase>>::iterator it = this->partitionItems.begin(); it != this->partitionItems.end(); ++it) {
    // TODO: figure out a cleaner way, dereferencing and taking the ptr seems terrible!
    this->outfile.write((char *)&*it->m_ptr, sizeof(*it->m_ptr));
    // write the contents
    this->outfile.write(it->m_ptr->iobuf, it->m_ptr->size);
   }

  // Clear partition-- for the next user
  this->partitionItems.clear();

  SET_HANDLER((MCacheSyncHandler) &RefCountCacheSync::pauseEvent);

  // TODO: no sleep between partitions?
  // TODO: keep track of how long it took and sleep if we were faster than expected
  // TODO: figure out how to get access to hostdb_sync_frequency (probaby should be passed in instead of this global magic)
  //e->schedule_in(MAX(MC_SYNC_MIN_PAUSE_TIME, HRTIME_SECONDS(hostdb_sync_frequency - 5) / MULTI_CACHE_PARTITIONS));
  //e->schedule_in(MC_SYNC_MIN_PAUSE_TIME);
  e->schedule_imm();
  return EVENT_CONT;
}



int RefCountCacheSync::pauseEvent(int event, Event *e) {
    (void) event;
    (void) e;

    // Schedule up the next partition
    if (partition < cc->partition_count())
      mutex = cc->lock_for_partition(partition);
    else
      mutex = cont->mutex;
    SET_HANDLER((MCacheSyncHandler) &RefCountCacheSync::copyPartition);
    e->schedule_imm();
    return EVENT_CONT;
}

// Open the tmp file, etc.
int RefCountCacheSync::initializeStorage(int event, Event *e) {
  this->filename = this->cc->get_filepath();
  this->tmp_filename =  this->filename + ".syncing";

  this->outfile.open(this->tmp_filename.c_str(), std::ios::out|std::ios::binary);

  if (! this->outfile.is_open()){
    Warning("Unable to create temporyary file %s, unable to persist hostdb\n", this->tmp_filename.c_str());
    delete this;
    return EVENT_DONE;
  }

  // Write out the header
  this->outfile.write((char *) this->cc->get_header(), sizeof(RefCountCacheHeader));

  SET_HANDLER((MCacheSyncHandler) &RefCountCacheSync::copyPartition);
  e->schedule_imm();
  return EVENT_CONT;
}
// do the final mv and close of file handle
int RefCountCacheSync::finalizeSync(){
  // move the file
  int ret;
  ret = rename(this->tmp_filename.c_str(), this->filename.c_str());

  if (ret != 0) {
    return ret;
  }

  // close the fd
  this->outfile.close();

  return 0;
}

RefCountCacheSync::RefCountCacheSync(Continuation *acont, RefCountCacheBase *cc) :
    Continuation(NULL), partition(0), cc(cc), cont(acont) {
    mutex = cc->lock_for_partition(partition);


    SET_HANDLER((MCacheSyncHandler) &RefCountCacheSync::initializeStorage);
  }
