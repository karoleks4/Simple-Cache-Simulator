#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <math.h>

typedef enum {dm, fa} cache_map_t;
typedef enum {uc, sc} cache_org_t;
typedef enum {instruction, data} access_t;

typedef struct {
    uint32_t address;
    access_t accesstype;
} mem_access_t;

typedef struct {
    int valid_bit;
    int tag;
    int fifo_counter; 
    /* This counter is only used for fully associative cache. It indicates the "oldest" block in the cache 
    (a block which hasn't been updated for the longest). This allows us to point out where the next address
    should be stored in cache. This counter is equal to 0 for the block in which the hit was recorded. It is
    incremented by 1 for every block during every search in the cache. */
} block_t; 

typedef struct {
    block_t *block_array;
} cache_t; 

// DECLARE CACHES AND COUNTERS FOR THE STATS


uint32_t cache_size; 
uint32_t block_size = 64;
cache_map_t cache_mapping;
cache_org_t cache_org;

// for UC:
cache_t cache;
int cache_counter = 0;
int hit_counter = 0;

// for FA:
// instruction:
cache_t i_cache;
int i_counter = 0;
int i_hit_counter = 0;
// data:
cache_t d_cache;
int d_counter = 0;
int d_hit_counter = 0;


/* Reads a memory access from the trace file and returns
 * 1) access type (instruction or data access)
 * 2) memory address
 */
mem_access_t read_transaction(FILE *ptr_file) {
    char buf[1000];
    char* token;
    char* string = buf;
    mem_access_t access;

    if (fgets(buf,1000, ptr_file)!=NULL) {

        /* Get the access type */
        token = strtok(string, " \n");
        if (strcmp(token,"I") == 0) {
            access.accesstype = instruction;
        } else if (strcmp(token,"D") == 0) {
            access.accesstype = data;
        } else {
            printf("Unkown access type\n");
            exit(0);
        }
        
        /* Get the access type */        
        token = strtok(NULL, " \n");
        access.address = (uint32_t)strtol(token, NULL, 16);
 

        return access;
    }

    /* If there are no more entries in the file,  
     * return an address 0 that will terminate the infinite loop in main
     */
    access.address = 0;
    return access;
}

/*  This function is used to initialise the cache. It takes number of blocks and a pointer to the cache we want to initialise as arguments.
    Every block is initialised with 64 bytes (according to the instruction) and all variables are initialised to 0. */
void setting_up (int block_number, cache_t * s_cache) {
    s_cache->block_array = malloc(block_number * 64);

    for (int i= 0; i< block_number; i++) {
        s_cache->block_array[i].tag = 0;
        s_cache->block_array[i].valid_bit = 0;
        s_cache->block_array[i].fifo_counter = 0;
    }
}

/*  This function is used for direct mapped cache. */
void execute_dm (mem_access_t access, int block_number, int index_size, cache_t * f_cache) {
    int cutOffset = (access.address >> 6) % (block_number);     // getting rid of the offset
    int tag = (access.address >> (6 + index_size));             // calculating the size of the tag (remaining bits after subtracting the size of the index and the offset (in bites))
    int f_counter = 0;                                          // initialising the counters
    int f_hit_counter = 0;

    for (int i= 0; i < block_number; i++) {                     // searching the cache
        if (i == cutOffset) {
            f_counter++;
            if (tag == f_cache->block_array[i].tag) {           // the tag is found in the cache 
                if (f_cache->block_array[i].valid_bit == 1) {   // valid bit is equal to 1
                    f_hit_counter++;                            // hit
                } else {                                        // valid bit is not equal to 1
                    f_cache->block_array[i].tag = tag;          // updating the tag
                    f_cache->block_array[i].valid_bit = 1;      // changing the valid bit to 1
                }
            } else {                                            // the tag is not found in the cache
                f_cache->block_array[i].tag = tag;              // updating the tag
                f_cache->block_array[i].valid_bit = 1;          // changing the valid bit to 1
            }
        }
    }

    /* Updating the counters: */
    if (f_cache == &cache) {                                    // for unified cache
        cache_counter += f_counter;
        hit_counter += f_hit_counter;
    }
    else if (f_cache == &i_cache) {                             // for instruction cache in splitted cache
        i_counter += f_counter;
        i_hit_counter += f_hit_counter;
    } else {                                                    // for data cache in splitted cache
        d_counter += f_counter;
        d_hit_counter += f_hit_counter;
    }
}

/*  This function is used for fully associative cache. */
void execute_fa (mem_access_t access, int block_number, cache_t * f_cache) {
    int tag = (access.address >> 6);    // calculating the size of the tag (remaining bits after cutting the offset)
    /* Initialising the counters */
    int f_counter = 0;                                          
    int f_hit_counter = 0;
    int found = 0;                      // used to indicate if the cache block was updated (if the hit was recorded or the block was updated because it was previously empty)                                  
    int max_counter = 0;                // used to find the maximum fifo_counter (the oldest updated cache block)
    int x = 0;                          // used to update fifo counters of the ramaining cache blocks if the hit was recorded or the block was updated because it was previously empty

    for (int i= 0; i< block_number; i++) {                                                      // searching in cache
        if (tag == f_cache->block_array[i].tag && f_cache->block_array[i].valid_bit == 1) {     // the tag is found in the cache and the valid bit is equal to 1
            f_counter++;
            f_hit_counter++;                                                // hit
            f_cache->block_array[i].fifo_counter++;
            found = 1;
            x = i + 1;
            break;
        } else if (f_cache->block_array[i].tag == 0) {                      // if the cache block is empty
            f_cache->block_array[i].tag = tag;                              // updating the tag
            f_cache->block_array[i].valid_bit = 1;                          // changing the valid bit to 1
            f_cache->block_array[i].fifo_counter = 0;                       // changing fifo_counter to 0 (the most recently updated block)
            found = 1;
            x = i + 1;
            f_counter++;
            break;
        } else {
            f_cache->block_array[i].fifo_counter++;
        }
    }

    if (found == 1) {                                                       // if the hit was recorded or the block was updated because it was previously empty
        for (int i = x; i < block_number; i++) {
            f_cache->block_array[i].fifo_counter++;
        }  
    }

    if (found == 0) {                                                       // if the tag wasn't matched 
        f_counter++;
        for (int a = 0; a < block_number; a++) {                            // find the maximum fifo_counter ("the oldest" cache block)
            if (f_cache->block_array[a].fifo_counter > max_counter) {
                max_counter = f_cache->block_array[a].fifo_counter;
            }
        }
        for (int b = 0; b < block_number; b++) {
            if (f_cache->block_array[b].fifo_counter == max_counter) {      // update the cache block which had the highest fifo_counter (which hasn't been updated for the longest)
                f_cache->block_array[b].tag = tag;
                f_cache->block_array[b].valid_bit = 1;
                f_cache->block_array[b].fifo_counter = 0;                   // changing fifo_counter to 0 (the most recently updated block)
                break;
            }
        }               
    }

    /* Updating the counters: */
    if (f_cache == &cache) {                        // for unified cache
        cache_counter += f_counter;
        hit_counter += f_hit_counter;
    }
    else if (f_cache == &i_cache) {                 // for instruction cache in splitted cache
        i_counter += f_counter;
        i_hit_counter += f_hit_counter;
    } else {                                        // for data cache in splitted cache
        d_counter += f_counter;
        d_hit_counter += f_hit_counter;
    }
}

void main(int argc, char** argv) {

    /* Read command-line parameters and initialize:
     * cache_size, cache_mapping and cache_org variables
     */

    if ( argc != 4 ) { /* argc should be 2 for correct execution */
        printf("Usage: ./cache_sim [cache size: 128-4096] [cache mapping: dm|fa] [cache organization: uc|sc]\n");
        exit(0);
    } else  {
        /* argv[0] is program name, parameters start with argv[1] */


        /* Set cache size */
        cache_size = atoi(argv[1]);

        /* Set Cache Mapping */
        if (strcmp(argv[2], "dm") == 0) {
            cache_mapping = dm;
        } else if (strcmp(argv[2], "fa") == 0) {
            cache_mapping = fa;
        } else {
            printf("Unknown cache mapping\n");
            exit(0);
        }

        /* Set Cache Organization */
        if (strcmp(argv[3], "uc") == 0) {
            cache_org = uc;
        } else if (strcmp(argv[3], "sc") == 0) {
            cache_org = sc;
        } else {
            printf("Unknown cache organization\n");
            exit(0);
        }
    }


    /* Open the file mem_trace.txt to read memory accesses */
    FILE *ptr_file;
    ptr_file =fopen("mem_trace.txt","r");
    if (!ptr_file) {
        printf("Unable to open the trace file\n");
        exit(1);
    }

    // Cache initialisation:    
    if (cache_mapping == fa || cache_mapping == dm) {
        if (cache_org == uc) {
            int block_number = cache_size / block_size;
            setting_up(block_number, &cache);
        } else { // (cache_org == sc)
            int block_number = (cache_size / 2) / block_size;
            setting_up(block_number, &i_cache);
            setting_up(block_number, &d_cache);
        }
    }

    /* Loop until whole trace file has been read */
    mem_access_t access;
    while(1) {
        access = read_transaction(ptr_file);
        //If no transactions left, break out of loop
        if (access.address == 0)
            break;

	   /* Do a cache access */

        else if (cache_mapping == dm) {
            if (cache_org == uc) {
                int block_number = cache_size / block_size;
                int index_size = (int)log2(block_number);
                execute_dm (access, block_number, index_size, &cache);
            } else {// (cache_org == sc)
                int block_number = (cache_size / 2) / block_size;
                int index_size = (int)log2(block_number);
                if (access.accesstype == instruction) {
                    execute_dm (access, block_number, index_size, &i_cache);
                } else { // (access.accesstype == data)
                    execute_dm (access, block_number, index_size, &d_cache);
                }
            }
        } else { // (cache_mapping == fa)
            if (cache_org == uc) {
                int block_number = cache_size / block_size;
                execute_fa (access, block_number, &cache);
            } else { // (cache_org == sc)
                int block_number = (cache_size/ 2) / block_size;
                if (access.accesstype == instruction) {
                    execute_fa (access, block_number, &i_cache);
                } else { // (access.accesstype == data)
                    execute_fa (access, block_number, &d_cache);
                }
            }
        }
    }

    /* Print the statistics */

    if (cache_org == uc) {
        printf("U.accesses: %d\n", cache_counter);
        printf("U.hits: %d\n", hit_counter);
        printf("U.hit rate: %1.3f\n", ((double)hit_counter)/cache_counter);
    } else { // cache_org == sc
        printf("I.accesses: %d\n", i_counter);
        printf("I.hits: %d\n", i_hit_counter);
        printf("I.hit rate: %1.3f\n\n", ((double)i_hit_counter)/i_counter);
        printf("D.accesses: %d\n", d_counter);
        printf("D.hits: %d\n", d_hit_counter);
        printf("D.hit rate: %1.3f\n", ((double)d_hit_counter)/d_counter);
    }

    /* Close the trace file */
    fclose(ptr_file);

}
