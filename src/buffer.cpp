/**
 * @author See Contributors.txt for code contributors and overview of BadgerDB.
 *
 * @section LICENSE
 * Copyright (c) 2012 Database Group, Computer Sciences Department, University of Wisconsin-Madison.
 */

#include <memory>
#include <iostream>
#include "buffer.h"
#include "exceptions/buffer_exceeded_exception.h"
#include "exceptions/page_not_pinned_exception.h"
#include "exceptions/page_pinned_exception.h"
#include "exceptions/bad_buffer_exception.h"
#include "exceptions/hash_not_found_exception.h"

namespace badgerdb { 

BufMgr::BufMgr(std::uint32_t bufs)
	: numBufs(bufs) {
	bufDescTable = new BufDesc[bufs];

  for (FrameId i = 0; i < bufs; i++) 
  {
  	bufDescTable[i].frameNo = i;
  	bufDescTable[i].valid = false;
  }

  bufPool = new Page[bufs];

	int htsize = ((((int) (bufs * 1.2))*2)/2)+1;
  hashTable = new BufHashTbl (htsize);  // allocate the buffer hash table

  clockHand = bufs - 1;
}

//Flushes out all dirty pages and deallocates the buffer pool and the BufDesc table.
BufMgr::~BufMgr() 
{
  //Creates a pointer to each buffer in the descTable
  for (int i = 0; i < numBufs; i++){
    BufDesc *tmpbuf = &bufDescTable[i];
    //Checks for modified (but not 'saved') buffers and then writes them to mem
    if (tmpbuf->valid == true && tmpbuf->dirty == true){
      tmpbuf->file->writePage(tmpbuf->pageNo, &(bufPool[i]));
    }
  }
  //Removes the nessecary additional vars
  delete [] bufDescTable;
  delete [] bufPool;
}

//Advance clock to next frame in the buffer pool.
void BufMgr::advanceClock()
{
  //HALP
}

//
void BufMgr::allocBuf(FrameId & frame) 
{
  int numScanned = 0;
  bool found = false;

  while (numScanned < 2*numBufs) {
    advanceClock();
    numScanned++;

    //Checks for a valid segment of the clock 
    if (!bufDescTable[clockHand]->valid) {
      break;
    }

    //Checks to ensure this clock segment has not been used recently
    if (!bufDescTable[clockHand]->refbit) {
      //Checks to ensure this clock segment has not been pinned
      if (bufDescTable[clockHand]->pinCnt == 0) {
        //Remove the entry from the hash table and ACK that a position has been found
        hashTable->remove(bufDescTable[clockHand]->file, bufDescTable[clockHand]->pageNo);
        found = true;
        break;
      }
    }
    //Skips the clock segment and updates vars
    else {
      bufStats->accesses++;
      bufDescTable[clockHand]->refbit = false;
    }
  }
  //Throws execption if a buffer cannot be allocated
  if (!found && numScanned >= 2*numBufs){
    throw buffer_exceeded_exception();
  }
  //Write to the disk if the clock segment hasnt been written since an update (segment is dirty)
  if (bufDescTable[clockHand]->dirty){
    bufStats->diskwrites++;
    bufDescTable[clockHand]->file->writePage(bufDescTable[clockHand]->pageNo, bufPool[clockHand]);
  }

  bufDescTable[clockHand]->Clear();
  frame = clockHand;
}
	
void BufMgr::readPage(File* file, const PageId pageNo, Page*& page)
{
  FrameId frameNo = 0;
  //Attempts to load page from mem based on params 
  try {
    hashTable->lookup(file, pageNo, frameNo);
    bufDescTable[frameNo]->refbit = true;
    bufDescTable[frameNo]->pinCnt++;
    page = &bufPool[frameNo];
  }
  //
  catch (hash_not_found_exception e){
    allocBuf(frameNo);
    bufStats->diskreads++;
    bufPool[frameNo] = file->readPage(pageNo);
    hashTable->insert(file, pageNo, frameNo);
    bufDescTable[frameNo]->Set(file, pageNo);
    //Return a pointer to the frame containing the page via the page parameter
    page = &bufPool[frameNo];

  }
}

void BufMgr::unPinPage(File* file, const PageId pageNo, const bool dirty) 
{
  FrameId frameNo = 0;
  hashTable->lookup(file, pageNo, frameNo);

  if (dirty == true){
    bufDescTable[frameNo].dirty = true;
  }

  if (bufDescTable[frameNo]->pinCnt == 0){
    throw page_not_pinned_exception(file->filename(), pageNo, frameNo);
  }
  bufDescTable[frameNo]->pinCnt--;
}

void BufMgr::flushFile(const File* file) 
{
  for (int i = 0; i < numBufs; i++){
    BufDesc *tmpbuf = &(bufDescTable[i])
    //'Scan[s] bufTable for pages belonging to the file'
    if (tmpbuf->valid == true && tmpbuf->file == file){
      if (tmpbuf->pinCnt = 0){
        throw page_pinned_exception(file->filename(), tmpbuf->pageNo, tmpbuf->frameNo);
      }
      if (tmpbuf->dirty == true){
        tmpbuf->file->writePage(tmpbuf->pageNo, bufPool[i]);
        tmpbuf->dirty = false;
      }
      hashTable->remove(file, tmpbuf->pageNo);
      tmpbuf->Clear();
    }
    else if (tmpbuf->valid == false && tmpbuf->file == file){
      throw bad_buffer_exception(tmpbuf->frameNo, tmpbuf->dirty, tmpbuf->valid, tmpbuf->refbit);
    }
  }
}

void BufMgr::allocPage(File* file, PageId &pageNo, Page*& page) 
{
  FrameId frameNo;
  bufPool[frameNo] = file->allocPage(pageNo);
  allocBuf(frameNo);
  hashTable->insert(file, pageNo, frameNo);
  bufDescTable[frameNo].Set(file, pageNo);
  page = &bufPool[frameNo];

  return(page, frameNo);

}

void BufMgr::disposePage(File* file, const PageId PageNo)
{
    FrameId frameNo = 0;
    hashTable->lookup(file, pageNo, frameNo);

    bufDescTable[frameNo].Clear();
    hashTable->remove(file, pageNo);

    file->delete(pageNo);
}

void BufMgr::printSelf(void) 
{
  BufDesc* tmpbuf;
	int validFrames = 0;
  
  for (std::uint32_t i = 0; i < numBufs; i++)
	{
  	tmpbuf = &(bufDescTable[i]);
		std::cout << "FrameNo:" << i << " ";
		tmpbuf->Print();

  	if (tmpbuf->valid == true)
    	validFrames++;
  }

	std::cout << "Total Number of Valid Frames:" << validFrames << "\n";
}

}
