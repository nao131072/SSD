/*
  FUSE ssd: FUSE ioctl example
  Copyright (C) 2008       SUSE Linux Products GmbH
  Copyright (C) 2008       Tejun Heo <teheo@suse.de>
  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.
*/
#define FUSE_USE_VERSION 35
#include <fuse3/fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include "ssd_fuse_header.h"
#define SSD_NAME "ssd_file"
enum
{
    SSD_NONE,
    SSD_ROOT,
    SSD_FILE,
};

static size_t physic_size;
static size_t logic_size;
static size_t host_write_size;
static size_t nand_write_size;

typedef union pca_rule PCA_RULE;
union pca_rule
{
    unsigned int pca;
    struct
    {
        unsigned int page : 16;
        unsigned int block : 16;
    } fields;
};

PCA_RULE curr_pca;

unsigned int* L2P;

int reserve_block = PHYSICAL_NAND_NUM - 1;

int invalid_counts[PHYSICAL_NAND_NUM] = {0};

static int nand_write(const char* buf, int pca);
static int nand_read(char* buf, int pca);
static int nand_erase(int block);

static int find_empty_block()
{
    int is_blocks_empty[PHYSICAL_NAND_NUM] = {1};
    
    for (int i = 0; i < PHYSICAL_NAND_NUM; i++)
    {
        is_blocks_empty[i] = (invalid_counts[i] == 0);
    }
    
    for (int i = 0; i < L2P_SIZE; i++)
    {
        if (L2P[i] != INVALID_PCA)
        {
            PCA_RULE tmp_pca;
            tmp_pca.pca = L2P[i];
            is_blocks_empty[tmp_pca.fields.block] = 0;
        }
    }
    
    for (int i = 0; i < PHYSICAL_NAND_NUM; i++)
    {
        if (is_blocks_empty[i] && i != reserve_block)
        {
            return i;
        }
    }
    return -1;
}

static int get_most_invalid_block()
{
    int max_invalid_block = 0;
    
    // find the most dirty block
    for (int i = 0; i < PHYSICAL_NAND_NUM; i++)
    {
        printf("invalid_counts[%d] = %d\n", i, invalid_counts[i]);
        if (invalid_counts[i] > invalid_counts[max_invalid_block])
        {
            max_invalid_block = i;
        }
    }
    
    return max_invalid_block;
}

static int ftl_gc()
{
    // DONE
    /*
        Copilot code follows
    */
    //  1. Decide the source block to be erased
    //  2. Move all the valid data in source block to another block
    //  3. Update L2P table
    //  4. Erase the source block with invalid data
    
    // 1. Decide the source block to be erased
    // by choosing the most dirty block (the block with the most invalid data)    
    
    printf("-----------------Garbage Collection-----------------\n");
    int max_invalid_block = 0;
    
    // find the most dirty block
    for (int i = 0; i < PHYSICAL_NAND_NUM; i++)
    {
        printf("invalid_counts[%d] = %d\n", i, invalid_counts[i]);
        if (invalid_counts[i] > invalid_counts[max_invalid_block])
        {
            max_invalid_block = i;
        }
    }
    
    // 2. Move all the valid data in source block to another block
    curr_pca.fields.block = max_invalid_block;
    curr_pca.fields.page = 0;
    
    PCA_RULE to_pca;
    to_pca.fields.block = reserve_block;
    to_pca.fields.page = 0;
    
    for (int i = 0; i < PAGE_PER_BLOCK; i++)
    {
        int lba = max_invalid_block * PAGE_PER_BLOCK + i;
        
        if (L2P[lba] != INVALID_PCA)
        {
            printf("Moving LBA %d from block %d to block %d\n", lba, curr_pca.fields.block, to_pca.fields.block);
            char buf[512];
            nand_read(buf, curr_pca.pca);
            nand_write(buf, to_pca.pca);
            // 3. Update L2P table
            L2P[lba] = to_pca.pca;
        }
        else
        {
            printf("Skipping LBA %d\n", lba);
        }
    }
    
    // 4. Erase the source block with invalid data
    nand_erase(max_invalid_block);
    invalid_counts[max_invalid_block] = 0;
    
    //5. Update the reserve block
    reserve_block = max_invalid_block;
    curr_pca.pca = to_pca.pca;
    if (to_pca.fields.page >= PAGE_PER_BLOCK){
        printf("After GC -> SSD STILL FULL\n");
        curr_pca.pca = FULL_PCA;
        return FULL_PCA;
    }
    
    return 0;
}

static int ssd_resize(size_t new_size)
{
    //set logic size to new_size
    if (new_size > LOGICAL_NAND_NUM * NAND_SIZE_KB * 1024)
    {
        return -ENOMEM;
    }
    else
    {
        logic_size = new_size;
        return 0;
    }

}

static int ssd_expand(size_t new_size)
{
    //logical size must be less than logic limit

    if (new_size > logic_size)
    {
        return ssd_resize(new_size);
    }

    return 0;
}

static int nand_read(char* buf, int pca)
{
    char nand_name[100];
    FILE* fptr;

    PCA_RULE my_pca;
    my_pca.pca = pca;
    snprintf(nand_name, 100, "%s/nand_%d", NAND_LOCATION, my_pca.fields.block);

    //read from nand
    if ( (fptr = fopen(nand_name, "r") ))
    {
        fseek( fptr, my_pca.fields.page * 512, SEEK_SET );
        fread(buf, 1, 512, fptr);
        fclose(fptr);
    }
    else
    {
        printf("open file fail at nand read pca = %d\n", pca);
        return -EINVAL;
    }
    return 512;
}

static int nand_write(const char* buf, int pca)
{
    char nand_name[100];
    FILE* fptr;

    PCA_RULE my_pca;
    my_pca.pca = pca;
    snprintf(nand_name, 100, "%s/nand_%d", NAND_LOCATION, my_pca.fields.block);

    //write to nand
    if ( (fptr = fopen(nand_name, "r+")))
    {
        fseek( fptr, my_pca.fields.page * 512, SEEK_SET );
        fwrite(buf, 1, 512, fptr);
        fclose(fptr);
        physic_size ++;
    }
    else
    {
        printf("open file fail at nand (%s) write pca = %d, return %d\n", nand_name, pca, -EINVAL);
        return -EINVAL;
    }

    nand_write_size += 512;
    return 512;
}

static int nand_erase(int block)
{
    char nand_name[100];
	int found = 0;
    FILE* fptr;

    snprintf(nand_name, 100, "%s/nand_%d", NAND_LOCATION, block);

    //erase nand
    if ( (fptr = fopen(nand_name, "w")))
    {
        fclose(fptr);
    }
    else
    {
        printf("open file fail at nand (%s) erase nand = %d, return %d\n", nand_name, block, -EINVAL);
        return -EINVAL;
    }


	// if (found == 0)
	// {
	// 	printf("nand erase not found\n");
	// 	return -EINVAL;
	// }

    printf("nand erase %d pass\n", block);
    
    physic_size -= (NAND_SIZE_KB << 1);
    
    return 1;
}

static unsigned int get_next_pca()
{
    /*  TODO: seq A, need to change to seq B */
	/* DONE */
    
     if (curr_pca.pca == INVALID_PCA)
    {
        //init
        curr_pca.pca = 0;
        return curr_pca.pca;
    }
    else if (curr_pca.pca == FULL_PCA)
    {
        //ssd is full, no pca can be allocated
        printf("No new PCA\n");
        return FULL_PCA;
    }
    
    if (curr_pca.fields.page == (NAND_SIZE_KB * 1024 / 512)-1)
    {
        int next_block = find_empty_block();
        if (next_block == -1)
        {
            printf("SSD is full\n");
            curr_pca.pca = FULL_PCA;
            return FULL_PCA;
        }
        else
        {
            curr_pca.fields.page = 0;
            curr_pca.fields.block = next_block;
            printf("PCA = page %d, nand %d\n", curr_pca.fields.page, curr_pca.fields.block);
            return curr_pca.pca;
        }
    }
    else
    {
        curr_pca.fields.page++;
        printf("PCA = page %d, nand %d\n", curr_pca.fields.page, curr_pca.fields.block);
        return curr_pca.pca;
    }
}

static int ftl_read( char* buf, size_t lba)
{
    /*  TODO: 1. Check L2P to get PCA 2. Send read data into nand_read */
    /* DONE */
    PCA_RULE pca;
    pca.pca = L2P[lba];
    
    if (pca.pca == INVALID_PCA)
    {
        // no data
        return 0;
    }
    else
    {
        return nand_read(buf, pca.pca);
    }
}

static int ftl_write(const char* buf, size_t lba_rnage, size_t lba)
{
    /*  TODO: only basic write case, need to consider other cases */
    /* DONE */
    PCA_RULE pca;
    pca.pca = get_next_pca();
    
    if (pca.pca == FULL_PCA)
    {
        printf(" ---------------PCA is full----------------\n");
        // pca is full, need to do garbage collection
        if (ftl_gc() == FULL_PCA) return -ENOMEM;
        pca.pca = get_next_pca();
        return -ENOMEM;
    }
    
    if (nand_write(buf, pca.pca) > 0)
    {   
        // update L2P table, point to new pca
        if (L2P[lba] != INVALID_PCA)    //address not clear 
        {
            PCA_RULE tmp_pca;
            tmp_pca.pca = L2P[lba];
            /* mark dirty block*/
            
            invalid_counts[tmp_pca.fields.block]++;
        }
        L2P[lba] = pca.pca;
        return 512;
    }
    else
    {
        printf(" --> Write fail !!!");
        return -EINVAL;
    }
}

static int ssd_file_type(const char* path)
{
    if (strcmp(path, "/") == 0)
    {
        return SSD_ROOT;
    }
    if (strcmp(path, "/" SSD_NAME) == 0)
    {
        return SSD_FILE;
    }
    return SSD_NONE;
}

static int ssd_getattr(const char* path, struct stat* stbuf,
                       struct fuse_file_info* fi)
{
    (void) fi;
    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();
    stbuf->st_atime = stbuf->st_mtime = time(NULL);
    switch (ssd_file_type(path))
    {
        case SSD_ROOT:
            stbuf->st_mode = __S_IFDIR | 0755;
            stbuf->st_nlink = 2;
            break;
        case SSD_FILE:
            stbuf->st_mode = __S_IFREG | 0644;
            stbuf->st_nlink = 1;
            stbuf->st_size = logic_size;
            break;
        case SSD_NONE:
            return -ENOENT;
    }
    return 0;
}

static int ssd_open(const char* path, struct fuse_file_info* fi)
{
    (void) fi;
    if (ssd_file_type(path) != SSD_NONE)
    {
        return 0;
    }
    return -ENOENT;
}

static int ssd_do_read(char* buf, size_t size, off_t offset)
{
    /*  TODO: call ftl_read function and handle result */
    /* DONE */
    int tmp_lba, tmp_lba_range, rst;
    char* tmp_buf;

    // out of limit
    if ((offset) >= logic_size)
    {
        return 0;
    }
    if ( size > logic_size - offset)
    {
        //is valid data section
        size = logic_size - offset;
    }

    tmp_lba = offset / 512;
	tmp_lba_range = (offset + size - 1) / 512 - (tmp_lba) + 1;
    tmp_buf = calloc(tmp_lba_range * 512, sizeof(char));

    for (int i = 0; i < tmp_lba_range; i++) {
        // TODOS
        int step = i * 512;
        // read data from ftl and store in tmp_buf
        rst = ftl_read(tmp_buf + step, tmp_lba + i);
        if (rst < 0)
        {
            free(tmp_buf);
            return rst;
        }
        
        if (rst == 0)
        {
           // no data, rewrite data to 0
           memset(tmp_buf + step, 0, 512);
        }
    }

    memcpy(buf, tmp_buf + offset % 512, size);

    free(tmp_buf);
    return size;
}

static int ssd_read(const char* path, char* buf, size_t size,
                    off_t offset, struct fuse_file_info* fi)
{
    printf(" --> Read size: %ld, offset: %ld\n", size, offset);
    (void) fi;
    if (ssd_file_type(path) != SSD_FILE)
    {
        return -EINVAL;
    }
    
    int result = ssd_do_read(buf, size, offset);
    get_most_invalid_block();
    return result;
}

static int ssd_do_write(const char* buf, size_t size, off_t offset)
{
    /*  TODO: only basic write case, need to consider other cases */
    /* DONE */
    
    
    // 1. align offset
    // 2. align size
    
    int tmp_lba, tmp_lba_range, processed_size;
    int idx, remaining_size, rst;

    host_write_size += size;
    if (ssd_expand(offset + size) != 0)
    {
        return -ENOMEM;
    }
    
    tmp_lba = offset / 512;
    tmp_lba_range = (offset + size - 1) / 512 - (tmp_lba) + 1;

    processed_size = 0;
    remaining_size = size;
    
    for (idx = 0; idx < tmp_lba_range; idx++)
    {   
        /*  example only align 512, need to implement other cases  */
        // case 1 : offset is not aligned
        // case 2 : size is not aligned
        
        int aligned_offset = offset % 512;
        int aligned_size = 512 - aligned_offset;
        int writing_size = aligned_size < remaining_size ? aligned_size : remaining_size;
        
        // write data to ftl
        if (aligned_offset == 0 && writing_size == 512)
        {
            // Aligned to 512B, send FTL-write API by LBA
            rst = ftl_write(buf + processed_size, writing_size, tmp_lba + idx);
            if ( rst == 0 )
            {
                //write full return -enomem;
                return -ENOMEM;
            }
            else if (rst < 0)
            {
                //error
                return rst;
            }
        }
        else
        {
            // Not aligned to 512B, read data from FTL
            char* read_buf = calloc(512, sizeof(char));
            rst = ftl_read(read_buf, tmp_lba + idx);
            if (rst < 0)
            {
                free(read_buf);
                return rst;
            }

            memcpy(read_buf + aligned_offset, buf + processed_size, writing_size);

            rst = ftl_write(read_buf, 512, tmp_lba + idx);
            free(read_buf);
        }
        
        remaining_size -= writing_size;
        processed_size += writing_size;
        offset += writing_size;
    }

    return size;
}

static int ssd_write(const char* path, const char* buf, size_t size,
                     off_t offset, struct fuse_file_info* fi)
{
    (void) fi;
    if (ssd_file_type(path) != SSD_FILE)
    {
        return -EINVAL;
    }
    
    int result = ssd_do_write(buf, size, offset);
    get_most_invalid_block();
    return result;
}

static int ssd_truncate(const char* path, off_t size,
                        struct fuse_file_info* fi)
{
    (void) fi;
    if (ssd_file_type(path) != SSD_FILE)
    {
        return -EINVAL;
    }

    return ssd_resize(size);
}

static int ssd_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info* fi,
                       enum fuse_readdir_flags flags)
{
    (void) fi;
    (void) offset;
    (void) flags;
    if (ssd_file_type(path) != SSD_ROOT)
    {
        return -ENOENT;
    }
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    filler(buf, SSD_NAME, NULL, 0, 0);
    return 0;
}

static int ssd_ioctl(const char* path, unsigned int cmd, void* arg,
                     struct fuse_file_info* fi, unsigned int flags, void* data)
{   
    if (ssd_file_type(path) != SSD_FILE)
    {
        return -EINVAL;
    }
    if (flags & FUSE_IOCTL_COMPAT)
    {
        return -ENOSYS;
    }
    switch (cmd)
    {
        case SSD_GET_LOGIC_SIZE:
            *(size_t*)data = logic_size;
            printf(" --> logic size: %ld\n", logic_size);
            return 0;
        case SSD_GET_PHYSIC_SIZE:
            *(size_t*)data = physic_size;
            printf(" --> physic size: %ld\n", physic_size);
            return 0;
        case SSD_GET_WA:
            *(double*)data = (double)nand_write_size / (double)host_write_size;
            return 0;
    }
    return -EINVAL;
}

static const struct fuse_operations ssd_oper =
{
    .getattr        = ssd_getattr,
    .readdir        = ssd_readdir,
    .truncate       = ssd_truncate,
    .open           = ssd_open,
    .read           = ssd_read,
    .write          = ssd_write,
    .ioctl          = ssd_ioctl,
};

int main(int argc, char* argv[])
{
    int idx;
    char nand_name[100];
    physic_size = 0;
    logic_size = 0;
	nand_write_size = 0;
	host_write_size = 0;
    curr_pca.pca = INVALID_PCA;
    L2P = malloc(LOGICAL_NAND_NUM * NAND_SIZE_KB * 1024 / 512 * sizeof(int));  // 5 * 10 * 1024 / 512 = 100
    memset(L2P, INVALID_PCA, sizeof(int)*LOGICAL_NAND_NUM * NAND_SIZE_KB * 1024 / 512);
    // for (int i = 0; i < LOGICAL_NAND_NUM; i++) invalid_counts[i] = PAGE_PER_BLOCK; 
    
    //create nand file
    for (idx = 0; idx < PHYSICAL_NAND_NUM; idx++)
    {
        FILE* fptr;
        snprintf(nand_name, 100, "%s/nand_%d", NAND_LOCATION, idx);
        fptr = fopen(nand_name, "w");
        if (fptr == NULL)
        {
            printf("open fail\n");
            return -1;
        }
        fclose(fptr);
    }
    
    return fuse_main(argc, argv, &ssd_oper, NULL);
}
