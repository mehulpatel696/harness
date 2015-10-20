/*
 * cachetest.c -- Test implementation of multithreaded file cache
 *
 * The disk has NBLOCKS of data; the cache stores many fewer.
 * Our stub assumes a cache size of one block, the trivial case.
 */

#include <stdlib.h>
#include <stdio.h>
#include "sthread.h"
#include <stdbool.h>
#include <math.h>
#include <string.h>

#define NTHREADS 10
#define NTESTS 10
#define NBLOCKS 100
#define BLOCKSIZE sizeof(int)

static void tester(int n);
static void cacheinit();
static void readblock(char *, int);
static void writeblock(char *, int);

/* the data being stored and fetched */
static char blockData[NBLOCKS][BLOCKSIZE];

/* randomblock
 * Generate a random block # from 0..NBLOCKS-1, according to a zipf
 * distribution, using the rejection method.  The C library random() gives
 * us a uniform distribution, and we discard each option with probability
 * 1-1/blocknum
 */

int randomblock() {
    int candidate;
    
    for (;;) {
        candidate = rand() % NBLOCKS;
        if ((double) rand()/RAND_MAX < (double) 1/(candidate + 1))
            return candidate;
    }
}

/* read/write 100 blocks, randomly distributed */
void tester(int n)
{
    int i, blocknum;
    char block[BLOCKSIZE];
    
    for (i = 0; i < NTESTS; i++) {
        blocknum = randomblock();
        if (rand() % 2) {	/* if odd, simulate a write */
            *(int *)block = n * NBLOCKS + blocknum;
            writeblock(block, blocknum); /* write the new value */
            printf("Wrote block %2d in thread %d: %3d\n", blocknum, n, *(int *)block);
        }
        else {		/* if even, simulate a read */
            readblock(block, blocknum);
            printf("Read  block %2d in thread %d: %3d\n", blocknum, n, *(int *)block);
        }
    }
    sthread_exit(100 + n);
    // Not reached
}

int main(int argc, char **argv)
{
    int i;
    long ret;
    sthread_t testers[NTHREADS];
    
    srand(0);	/* init the workload generator */
    cacheinit();  /* init the buffer */
    
    /* init blocks */
    for (i = 0; i < NBLOCKS; i++) {
        memcpy(blockData[i], (char *) &i, BLOCKSIZE);
    }
    
    /* start the testers */
    for(i = 0; i < NTHREADS; i++){
        sthread_create(&(testers[i]), &tester, i);
    }
    /* wait for everyone to finish */
    for(i = 0; i < NTHREADS; i++){
        ret = sthread_join(testers[i]);
    }
    printf("Main thread done.\n");
    return ret;
}

/* simulated disk block routines
 * simulate out of order completion by the disk
 * by sleeping for up to 100us */
void dblockread(char *block, int blocknum) {
    memcpy(block, blockData[blocknum], BLOCKSIZE);
    sthread_sleep(0, rand() % 100000);
}
void dblockwrite(char *block, int blocknum) {
    memcpy(blockData[blocknum], block, BLOCKSIZE);
    sthread_sleep(0, rand() % 100000);
}

/* stub routines
 * We've implemented a single item cache in a particularly inefficient fashion.
 */

#define INVALID -1	// cache starts empty
#define CACHESIZE 10 // cache size

struct blockcache {
    smutex_t mutex;
    int blocknum;		// blocknumber of the current block in the cache
    bool dirty;		// whether the block is dirty
    char block[BLOCKSIZE]; // storage for the block of data
};

/* mutual exclusion */
static smutex_t mutex;
static struct blockcache cache[CACHESIZE];
//---------------------------------------------------------------------------------->
static int orderArray[CACHESIZE];
// holds indices of blocks in blockcache
// when a block needs to be put in, it replaces block at index at front of this
// when a block is initialized/reused, its index is put at the end of orderArray

static int orderCount;

// if orderArray is being reshuffled, orderCount == -1
// if orderArray is not accessed by anyone, orderCount == 0
// if orderArray is accessed by threads, orderCount > 0
static scond_t orderZero; // signals that orderCount is 0
static scond_t orderPositive; // signals that orderCount is >= 0


/* Cache routines */

// Reshuffles the orderArray
// orderArray must be protected during reshuffling! - not provided in function
void putToEnd(int indexTemp) {
    // indexTemp is the index in orderArray that needs to be put to end of it
    // notice that indexTemp refers to *contents* of orderArray, not its indices
    
    int i;
    int startPosition = 0; // from which place up do we reshuffle
    
    for (i = 0; i < CACHESIZE; i++) { // look through the orderArray
        if (orderArray[i] == indexTemp) { // find indexTemp in orderArray
            startPosition = i; // this is the first place to reshuffle
        }
    }
    
    for (i = startPosition; i < (CACHESIZE-1); i++) { // reshuffling
        orderArray[i] = orderArray[i+1]; // move things up
    }
    orderArray[CACHESIZE-1] = indexTemp; // put indexTemp at the end
}
//----------------------------------------------------------------------------------<

void cacheinit() {
    scond_init(&orderZero);
    scond_init(&orderPositive);
    smutex_init(&mutex);
    //cache.dirty = false;
    //cache.blocknum = INVALID;
    int i;
    orderCount = 0; // make sure orderCount is initialized
    for (i = 0; i < CACHESIZE; i++ ) { // initialize all blockcaches
        smutex_init(&cache[i].mutex);
        cache[i].dirty = false;
        cache[i].blocknum = INVALID;
    }
    
    for (i = 0; i < CACHESIZE; i++) { // initialize orderArray with 0-CACHESIZE
        // needs to be this way because we initially, we allocate stuff in order
        orderArray[i] = i;
    }
}



// EDITED
void readblock(char *block, int blocknum) {
    
    //The Counter
    int i;
    //If Cached if found
    int foundCache = -1;
    //The index to replace
    int replaceIndex = 0;
    
    smutex_lock(&mutex);
    if (countOrder == 0) scond_broadcast(&orderZero, &mutex);
    if (countOrder >= 0) scond_broadcast(&orderPositive, &mutex);
    
    //Unlock
    smutex_unlock(&mutex);
    //Lock
    smutex_lock(&mutex);
    
    while (countOrder < 0) scond_wait(&orderPositive, &mutex);
    
    countOrder += 1;
    smutex_unlock(&mutex);
    
    for (i = 0; i < CACHESIZE; i++) {
        if (cache[i].blocknum == blocknum) {
            foundCache = i;
            break;
        }
    }
    
    if (foundCache == -1) {
        replaceIndex = orderArray[0];
        smutex_lock(&cache[replaceIndex].mutex);
        
        if (cache[replaceIndex].dirty) {
            dblockwrite(cache[replaceIndex].block, cache[replaceIndex].blocknum);
        }
        
        cache[replaceIndex].blocknum = blocknum;
        cache[replaceIndex].dirty = false;
        dblockread(cache[replaceIndex].block, blocknum);
        
        memcpy(block, cache[replaceIndex].block, BLOCKSIZE);
        
        smutex_unlock(&cache[replaceIndex].mutex);
    }
    
    else {
        
        replaceIndex = foundCache;
        smutex_lock(&cache[replaceIndex].mutex);
        memcpy(block, cache[replaceIndex].block, BLOCKSIZE);
        smutex_unlock(&cache[replaceIndex].mutex);
        
    }
    
    smutex_lock(&mutex);
    countOrder -= 1;
    if (countOrder == 0) {
        scond_broadcast(&orderZero, &mutex);
    }
    if (countOrder >= 0) {
        scond_broadcast(&orderPositive, &mutex);
    }
    smutex_unlock(&mutex);
    
    smutex_lock(&mutex);
    while (countOrder != 0) {
        scond_wait(&orderZero, &mutex);
    }
    
    countOrder -= 1;
    smutex_unlock(&mutex);
    
    putToEnd(replaceIndex); // updates the orderArray
    
    smutex_lock(&mutex);
    countOrder += 1;
    scond_broadcast(&orderZero, &mutex);
    scond_broadcast(&orderPositive, &mutex);
    smutex_unlock(&mutex);
}


void writeblock(char *block, int blocknum) {
    
    int i;
    int foundCache = -1; // where is the block with correct blocknum in cache
    int replaceIndex = 0; // which index do we replace?
    
    // redundant, rebroadcast (to make sure the threads start)
    smutex_lock(&mutex);
    if (countOrder == 0) {
        scond_broadcast(&orderZero, &mutex);
    }
    if (countOrder >= 0) {
        scond_broadcast(&orderPositive, &mutex);
    }
    smutex_unlock(&mutex);
    
    // threads have to wait if countOrder is -1
    smutex_lock(&mutex);
    while (countOrder < 0) {
        scond_wait(&orderPositive, &mutex);
    }
    countOrder += 1;
    smutex_unlock(&mutex);
    
    for (i = 0; i < CACHESIZE; i++) {
        if (cache[i].blocknum == blocknum) {
            foundCache = i; // record where we found the correct block
            break;
        }
    }
    
    if (foundCache == -1) {
        replaceIndex = orderArray[0];
        smutex_lock(&cache[replaceIndex].mutex);
        
        if (cache[replaceIndex].dirty) {
            dblockwrite(cache[replaceIndex].block, cache[replaceIndex].blocknum);
        }
        
        cache[replaceIndex].blocknum = blocknum;
        cache[replaceIndex].dirty = true;
        memcpy(cache[replaceIndex].block, block, BLOCKSIZE);
        
        smutex_unlock(&cache[replaceIndex].mutex);
    }
    
    else {
        replaceIndex = foundCache;
        smutex_lock(&cache[replaceIndex].mutex);
        
        cache[replaceIndex].blocknum = blocknum;
        cache[replaceIndex].dirty = true;
        memcpy(cache[replaceIndex].block, block, BLOCKSIZE);
        
        smutex_unlock(&cache[replaceIndex].mutex); // unlock the blockcache
    }
    
    smutex_lock(&mutex);
    countOrder -= 1;
    if (countOrder == 0) {
        scond_broadcast(&orderZero, &mutex);
    }
    if (countOrder >= 0) {
        scond_broadcast(&orderPositive, &mutex);
    }
    smutex_unlock(&mutex);
    
    smutex_lock(&mutex);
    while (countOrder != 0) {
        scond_wait(&orderZero, &mutex);
    }
    countOrder -= 1;
    smutex_unlock(&mutex);
    
    putToEnd(replaceIndex); // updates the orderArray
    
    smutex_lock(&mutex);
    countOrder += 1;
    scond_broadcast(&orderZero, &mutex);
    scond_broadcast(&orderPositive, &mutex);
    smutex_unlock(&mutex);
}