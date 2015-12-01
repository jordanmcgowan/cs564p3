/*
 * Names: Jordan McGowan (ID: 9068269662), Brad Trapp (ID: 9067852294), Maxwell Beckers (ID: 9066226912)
 * Course: CS564
 * Section: 1 
 * Project: 3
 * Emails: jmmcgowan@wisc.edu, bbtrapp@wisc.edu, mbeckers@wisc.edu
 * 
 * This file is the main implementation for the Badger DB. It allows for the allocation of buffer pages and files
 * and also the implementation for reading and writing pages to/from memory. It functions as a buffer manager 
 * and is used in coordination with 'main.cpp' to run tests on the methods below. 
 * 
 * PLEASE NOTE: Any comments with the format " [line comment] " were 
 * taken from the project description. This was done to ensure no 
 * confusion on comments, not to be confused with laziness. 
 * 
 * NOTE: No other files in this dir have been edited by our group, 
 * all files will be in their original edition. 
 * 
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

	/*
	 * Constructor of BufMgr class
	 */
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

	BufMgr::~BufMgr() {
		//Steps thru all of the buffers
		for(std::uint32_t i = 0; i < numBufs; i++){
			//Checks to see if the buffer page is dirty
			if(bufDescTable[i].dirty){
				//Writes the page to memory
				flushFile(bufDescTable[i].file);    
			}
		}
		//Deallocates buffer pool and bufDescTable (and hashtable per Piazza post 253)
		delete[] bufPool;
		delete[] bufDescTable;
		delete hashTable;
	}

	void BufMgr::advanceClock(){
		//Advances the clockhand and ensures no overflow
		clockHand++;
		clockHand = clockHand % numBufs;
	}

	void BufMgr::allocBuf(FrameId & frame){
		std::uint32_t cnt = 0;
		//Step thru all the buffers
		for(std::uint32_t i = 0; i < numBufs; i++){
			advanceClock();
			BufDesc* tmpbuf = &(bufDescTable[clockHand]);
			//Make sure that the buffer frame allocated has a valid page in it
			if(tmpbuf->valid){
				//If the page has been pinned, update the 
				if(tmpbuf->pinCnt != 0){
					cnt++;
					//Throws BufferExceededException if all buffer frames are pinned
					if(cnt >= numBufs){
						throw BufferExceededException();
					}
					continue;
				}

				//Write the page to memory if the file is dirty and update dirty bit
				if(tmpbuf->dirty == true){
					flushFile(tmpbuf->file);
					tmpbuf->dirty = false;
				}

				//Removes page if it is valid and not pinned
				//"Make sure that if the buffer frame allocated has a valid page in it, you remove the appropriate entry from the hash table."
				try{
					if(tmpbuf->pinCnt == 0 && tmpbuf->valid) {
						hashTable->remove(tmpbuf->file, tmpbuf->pageNo);
					}
				break;;
				}
				catch(HashNotFoundException e){
					//Does nothing here, just catching friendly exceptions
				}
			}
			//Do nothing if the conditions are not met
			else{
				break;
			}
		}
		//Return the new frame number
		frame = bufDescTable[clockHand].frameNo;
	}

	void BufMgr::readPage(File* file, const PageId pageNo, Page*& page){

		FrameId fnum;

		//Check to see if the page is in the buffer pool
		try{
			hashTable->lookup(file, pageNo, fnum);
			//Case 2: Page is in the buffer pool
			//"Set the appropriate refbit"
			bufDescTable[fnum].refbit = true;
			//"Increment the pinCnt for the page"
			bufDescTable[fnum].pinCnt++;
			//"return a pointer to the frame containing the page via the page parameter."
			page = &bufPool[fnum];
		}
		catch(HashNotFoundException e){
			//Case 1: Page is not in the buffer
			//"Call allocBuf() to allocate a buffer frame"
			allocBuf(fnum);
			Page newpage;
			//"Call the method file->readPage() to read the page from disk into the buffer pool"
			newpage = file->readPage(pageNo);
			bufPool[fnum] = newpage;
			//"Insert the page into the hashtable"
			hashTable->insert(file, pageNo, fnum);
			//"Invoke Set() on the frame to set it up properly"
			bufDescTable[fnum].Set(file, pageNo);
			//"Return a pointer to the frame containing the page via the page parameter"
			page = &bufPool[fnum];
		}
	}

	void BufMgr::unPinPage(File* file, const PageId pageNo, const bool dirty){
		FrameId fnum;
		//Throws HashNotFound if cannot find
		try{
			hashTable->lookup(file, pageNo, fnum);

			//Setting dirty bit if pageNo is dirty
			if(dirty){
				bufDescTable[fnum].dirty = true;
			}
			//Checks to see if the page is pinned
			if(bufDescTable[fnum].pinCnt == 0){
				throw PageNotPinnedException(file->filename(), pageNo, fnum);
			}
			//"Decrements the pinCnt of the frame containing (file, PageNo)"
			bufDescTable[fnum].pinCnt--;
		}
		catch(HashNotFoundException e){
			//"Does nothing if page is not found in the hash table lookup"
		}
	}

	void BufMgr::allocPage(File* file, PageId &pageNo, Page*& page){
		//"Allocate an empty page in the specified file by invoking the file->allocatePage() method"
		Page newpage = file->allocatePage();

		//"allocBuf() is called to obtain a buffer pool frame"
		FrameId fnum;
		allocBuf(fnum);

		bufPool[fnum] = newpage;
		//"Page number of the newly allocated page to the caller via the pageNo parameter" (needed for insert() below)
		pageNo = newpage.page_number();
		//"Pointer to the buffer frame allocated for the page via the page parameter"
		page = &bufPool[fnum];

		//"An entry is inserted into the hash table and Set() is invoked on the frame to set it up properly"
		try{
			hashTable->insert(file, pageNo, fnum);
			bufDescTable[fnum].Set(file, pageNo);
		}
		catch(HashNotFoundException e){
			//Does nothing here, just catching friendly exceptions
		}


	}

	void BufMgr::disposePage(File *file, const PageId pageNo){
		FrameId fnum;

		//Throws HashNotFound if cannot find
		try{
			hashTable->lookup(file, pageNo, fnum);
			
			//Clear and remove 
			bufDescTable[fnum].Clear();
			hashTable->remove(file, pageNo);

			file->deletePage(pageNo);
		}
		catch(HashNotFoundException e){
			//"Does nothing if page is not found in the hash table lookup"
		}
	}

	void BufMgr::flushFile(const File* file){
		//"Should scan bufTable for pages belonging to the file"
		for(std::uint32_t i = 0; i < numBufs; i++){
			if(bufDescTable[i].file == file){
				BufDesc* tmpbuf = &(bufDescTable[i]);

				//Write the page to disk if it is dirty
				if(tmpbuf->dirty){
					tmpbuf->file->writePage(bufPool[i]);
					tmpbuf->dirty = false;
				}

				//Throw an exception if the page has been pinned previously 
				if(bufDescTable[i].pinCnt != 0){
					throw PagePinnedException(file->filename(), tmpbuf->pageNo, i);
				}

				//Throw an exception if the page is not valid
				if(!bufDescTable[i].valid){
					throw BadBufferException(i, tmpbuf->dirty, tmpbuf->valid, tmpbuf->refbit);
				}
				
				//"An entry is inserted into the hash table and Set() is invoked on the frame to set it up properly"
				try{
					//"Remove the page from the hashtable (whether the page is clean or dirty)"
					hashTable->remove(file, tmpbuf->pageNo);
					//"Invoke the Clear() method of BufDesc for the page frame"
					tmpbuf->Clear();
				}
				catch(HashNotFoundException e){
					//Does nothing here, just catching friendly exceptions
				}
			}
		} 
	}

	/*
	 * Print member variable values. 
	 */
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
