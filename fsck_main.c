/*
  18-746: FSCK for EXT2.
  Faraz Shaikh
  fshaikh@andrew.cmu.edu
*/

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <genhd.h>
#include <partition_manager.h>
#include <ext2_structures.h>
#include <ext2_namei.h>
#include <ext2_fs.h>
#include <ext2_namei.h>
#include <fsck_errors.h>
#include <bio.h>
#include <fsck_list.h>
#include <stdlib.h>
#include <string.h>



/****************************************************************
 *  filesystem_info                                             *
 *  desribes the filesystem information for the active partition*
 ****************************************************************/
struct filesystem_info{
    int    activePartitionIdx;
    struct ext2_super_block      ext2_super_block;
    struct ext2_group_desc      *pext2_group_desc;
};


/************************************************************************
 *fsck_context                                                          *
 *describes the fsck book-keeping information to be used for 1 iteration*
 *of fsck                                                               *
 ************************************************************************/
typedef struct _fsck_context {
    /* Disk information */
    fsck_partition_info_t   partition_info;

    /* Filesystem information */
    struct filesystem_info  fs_info;
    /* disk block */
    bio_t bio;

    struct fsck_list_header nsInodeListHeader;
    struct fsck_list_header nsDataListHeader;
}fsck_context_t,*pfsck_context_t;


/***************************************************************************************/
/* Common Code and utils                                                               */
/***************************************************************************************/
#define BITS_PER_BYTE 8


/***************************************************************************************
 *    safeGetBit                                                                       *
 *      Gets the value of a bit in a bitmap                                            *
 *   Input                                                                             *
 *   bitmap                                                                            *
 *      buffer pointing to the bitmap                                                  *
 *   length                                                                            *
 *      length of the buffer in bytes                                                  *
 *   bitnumber                                                                         *
 *      the bitnumber to be queried                                                    *
 ***************************************************************************************/
int safeGetBit(char *bitmap,int length,int bitnumber) {
    char mask = (char)0x1;
    if(bitnumber < 0) {
        //-printf("\n[WARN] Cannot Get bits at negetive bit offsets");
        return 0;
    }

    if(bitnumber/BITS_PER_BYTE >= length) {
        FSCK_LOG_ERROR("Bit is off bounds",safeGetBit,-1);
        return -1;
    }

    if(bitmap[bitnumber/BITS_PER_BYTE] & (mask << bitnumber%BITS_PER_BYTE))
        return 1;
    else
        return 0;

    return -1;
}




/***************************************************************************************
 *   safeSetBit                                                                        *
 *      Sets a specified bit to an given value                                         *
 *   Input                                                                             *
 *   bitmap                                                                            *
 *      buffer pointing to the bitmap                                                  *
 *   length                                                                            *
 *      length of the buffer in bytes                                                  *
 *   bitnumber                                                                         *
 *      the bitnumber to be set                                                        *
 *   value                                                                             *
 *      value of the bit to be set                                                     *
 ***************************************************************************************/
int safeSetBit(char *bitmap,int length,int bitnumber,int value) {
    char mask = (char)0x1;
    if(bitnumber < 0) {
        printf("\n Cannot Set bits at negetive bit offsets");
        return 0;
    }


    if(bitnumber/BITS_PER_BYTE >= length) {
        FSCK_LOG_ERROR("Bit is off bounds",safeGetBit,-1);
        return -1;
    }
    /* Set to 1*/
    if(value) {
        bitmap[bitnumber/BITS_PER_BYTE] = bitmap[bitnumber/BITS_PER_BYTE] | (mask << bitnumber%BITS_PER_BYTE);
    }else {/* Set to zero */
        bitmap[bitnumber/BITS_PER_BYTE] = bitmap[bitnumber/BITS_PER_BYTE] & ~(mask << bitnumber%BITS_PER_BYTE);
    }
    return 0;
}




/***********************************************/
/* print_usage                                 */
/*   prints the usage of the function.         */
/***********************************************/
void  print_usage(void) {
    printf("\nfsck diskname \n Ex: fsck disk1 or fsck /dev/hda1");
}





#define INODE_USED 1
#define INODE_FREE 0
#define DATA_USED  1
#define DATA_FREE  0

/***************************************************************************************/
/* PASS 4 Code and utils                                                               */
/***************************************************************************************/
int fsck_pass4_build_NS_data_list_hidden_blocks(pfsck_context_t pfsck_context) {
    struct element elt;
    int    i;
    int    j;
    int    blockSize;
    int    nrBlockGroups;
    int    inclusive_upper_bound;
    int    inclusive_lower_bound;
    int    inodeTableBlocks;
    int    ret;


    blockSize = (1024 << pfsck_context->fs_info.ext2_super_block.s_log_block_size);
    nrBlockGroups = pfsck_context->fs_info.ext2_super_block.s_inodes_count /
        pfsck_context->fs_info.ext2_super_block.s_inodes_per_group;


    /* We have to add some blocks that never show up in the name space */
    elt.idxNumber = pfsck_context->fs_info.ext2_super_block.s_first_data_block;
    elt.count     = 0;
    ret = fsck_list_add_element(&pfsck_context->nsDataListHeader,
                                &elt);
    if(ret) {
        printf("\n Failed to duild NSdatablock list for datablock %d",elt.idxNumber);
        return ret; // serious enough to bail out this partition completely
    }

    /* for each block group we have some entries that need to be added */
    inodeTableBlocks = pfsck_context->fs_info.ext2_super_block.s_inodes_per_group;
    inodeTableBlocks *= pfsck_context->fs_info.ext2_super_block.s_inode_size;
    inodeTableBlocks /= blockSize;
    if(pfsck_context->fs_info.ext2_super_block.s_inodes_per_group / blockSize) inodeTableBlocks++;

    for(i=0; i<nrBlockGroups;i++){
        inclusive_lower_bound = pfsck_context->fs_info.ext2_super_block.s_blocks_per_group * i;
        inclusive_upper_bound = pfsck_context->fs_info.ext2_super_block.s_blocks_per_group * (i + 1);
        inclusive_upper_bound--;

        // Add inode table blocks for this block group //
        for(j=0;j<inodeTableBlocks;j++) {
            elt.idxNumber = pfsck_context->fs_info.pext2_group_desc[i].bg_inode_table + j;
            elt.count     = 0;
            ret = fsck_list_add_element(&pfsck_context->nsDataListHeader,
                                        &elt);
            if(ret) {
                printf("\n Failed to duild NSdatablock list for datablock %d",elt.idxNumber);
                return ret; // serious enough to bail out this partition completely
            }
        }

        // Add block_bitmap for this group
        elt.idxNumber = pfsck_context->fs_info.pext2_group_desc[i].bg_block_bitmap;
        elt.count     = 0;
        ret = fsck_list_add_element(&pfsck_context->nsDataListHeader,
                                    &elt);
        if(ret) {
            printf("\n Failed to duild NSdatablock list for datablock %d",elt.idxNumber);
            return ret; // serious enough to bail out this partition completely
        }
        // Add inode_bitmap for this group.
        elt.idxNumber = pfsck_context->fs_info.pext2_group_desc[i].bg_inode_bitmap;
        elt.count     = 0;
        ret = fsck_list_add_element(&pfsck_context->nsDataListHeader,
                                    &elt);
        if(ret) {
            printf("\n Failed to duild NSdatablock list for datablock %d",elt.idxNumber);
            return ret; // serious enough to bail out this partition completely
        }

        // Account for any super block and group descriptors backup/originals that may be
        // present in this block group
        if(inclusive_lower_bound > pfsck_context->fs_info.pext2_group_desc[i].bg_block_bitmap) {
            printf("\nProbable Corruption of group descriptor and super block");
            return FATAL_ERROR;
        }

        for(j=inclusive_lower_bound;j<pfsck_context->fs_info.pext2_group_desc[i].bg_block_bitmap;j++){
            if(j==0) continue;
            elt.idxNumber = j;
            elt.count     = 0;
            ret = fsck_list_add_element(&pfsck_context->nsDataListHeader,
                                        &elt);
            if(ret) {
                printf("\n Failed to duild NSdatablock list for datablock %d",elt.idxNumber);
                return ret; // serious enough to bail out this partition completely
            }
        }

    }
    return 0;
}


int fsck_pass4_build_NS_data_list(pfsck_context_t pfsck_context){
    int    ret;
    __u32  inodeNumber;
    struct ext2_inode inodetobefixed;
    int    i;
    struct node *pNode;
    __u32  VCN;
    struct element elt;
    int    blockno;

    /* for every indode in the name space list */
    pNode = pfsck_context->nsInodeListHeader.next;
    while(pNode) {
        for(i=0;i<pNode->next_free_element;i++) {
            /* read the inode using get */
            ret = read_ext2_get_inode(
                                      &pfsck_context->partition_info,
                                      pfsck_context->fs_info.activePartitionIdx,
                                      &pfsck_context->fs_info.ext2_super_block,
                                      pfsck_context->fs_info.pext2_group_desc,
                                      pNode->elements[i].idxNumber,
                                      &inodetobefixed);
            if(ret) {
                printf("\n Cannot read  data pointers for indode %d: PASS4 Aborted",pNode->elements[i].idxNumber);
                continue; // ok to continue here
            }

            // build up the list.
            // printf("\n%d",pNode->elements[i].idxNumber);
            blockno=0;
            while(1) {
                ret = read_ext2_LCN_TO_VCN(
                                           &pfsck_context->partition_info,
                                           pfsck_context->fs_info.activePartitionIdx,
                                           &pfsck_context->fs_info.ext2_super_block,
                                           pfsck_context->fs_info.pext2_group_desc,
                                           &inodetobefixed,
                                           blockno++,
                                           &pfsck_context->nsDataListHeader,
                                           &VCN);
                if(FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS == ret)
                    {
                        ret = 0;
                        break;
                    }
                if(ret)
                    {
                        printf("Cannot get LCN->VCN mapping for inode %d for LCN %d",pNode->elements[i].idxNumber,blockno++);
                        break;
                    }

                //printf("\n=> %d,%d",blockno-1,VCN);
                // This is the case for sparse files //
                if(!VCN)
                    continue;


                // Add the block to list of reffered //
                elt.idxNumber = VCN;
                elt.count     = 0;
                ret = fsck_list_add_element(&pfsck_context->nsDataListHeader,
                                            &elt);
                if(ret) {
                    printf("\n Failed to duild NSdatablock list at inode %d",pNode->elements[i].idxNumber);
                    return ret; // serious enough to bail out this partition completely
                }
            }
        } // end for node
        pNode = pNode->next;
    } // end for list head

    ret = fsck_pass4_build_NS_data_list_hidden_blocks(pfsck_context);
    if(ret) {
        printf("\n Failed to duild NSdatablock for metadata blocks list");
        return FATAL_ERROR; // serious enough to bail out this partition completely
    }
    return 0;
}


int fsck_pass4_build_bitmap(pfsck_context_t pfsck_context,
                            int             blockGroupNo,
                            pbio_t           pBio)
{
    int inclusive_lower_bound;
    int inclusive_upper_bound;
    int i;
    int ret;
    struct node *pNode;

    /* calculate inclusive data bounds bounds for this block group */
    inclusive_lower_bound = pfsck_context->fs_info.ext2_super_block.s_blocks_per_group * blockGroupNo;
    inclusive_upper_bound = pfsck_context->fs_info.ext2_super_block.s_blocks_per_group * (blockGroupNo + 1);
    inclusive_upper_bound--;

    bio_clear_bio_block(pBio);

    /* for every indode in the name space list */
    pNode = pfsck_context->nsDataListHeader.next;
    while(pNode) {
        int bit_to_set;
        for(i=0;i<pNode->next_free_element;i++) {
            pNode->elements[i].idxNumber;
            if(pNode->elements[i].idxNumber < inclusive_lower_bound || pNode->elements[i].idxNumber > inclusive_upper_bound )
                continue;

            bit_to_set  =  pNode->elements[i].idxNumber;
            bit_to_set -=  inclusive_lower_bound;
            bit_to_set -=  pfsck_context->fs_info.ext2_super_block.s_first_data_block;

            ret = safeSetBit(pBio->buffer,pBio->length,
                             bit_to_set,
                             DATA_USED);
            if(ret) {
                printf("\nPASS4 Could not set the bit for datablock %d",pNode->elements[i].idxNumber);
                return ret;
            }
        } // end for node
        pNode = pNode->next;
    } // end for list head
    return 0;
}



int fsck_print_block_bitmap_error(pfsck_context_t pfsck_context,
                                  int             blockGroupNo,
                                  pbio_t          pBioCalculated,
                                  pbio_t          pBioOriginal)
{
    int inclusive_lower_bound;
    int inclusive_upper_bound;
    volatile int bitvalorig,bitvalcalculated;
    int i;

    /* calculate inclusive data bounds bounds for this block group */
    inclusive_lower_bound = pfsck_context->fs_info.ext2_super_block.s_blocks_per_group * blockGroupNo;
    inclusive_upper_bound = pfsck_context->fs_info.ext2_super_block.s_blocks_per_group * (blockGroupNo + 1);
    inclusive_upper_bound--;

    for(i=0;i<pfsck_context->fs_info.ext2_super_block.s_blocks_per_group;i++)
        {

            if(i+inclusive_lower_bound >=
               (pfsck_context->fs_info.ext2_super_block.s_blocks_count - pfsck_context->fs_info.ext2_super_block.s_first_data_block) )
                continue;

            bitvalorig = -1;
            bitvalcalculated = -1;

            /* is this bit set */
            bitvalorig = safeGetBit(pBioOriginal->buffer,pBioOriginal->length,i);
            if(-1 == bitvalorig) {
                printf("\nCould not read Bit from inode bitmap");
                printf("\nPartition %d,BlockGroup %d,BitOffset %d",pfsck_context->fs_info.activePartitionIdx,blockGroupNo,i);
                return(bitvalorig);
            }

            bitvalcalculated = safeGetBit(pBioCalculated->buffer,pBioCalculated->length,i);
            if(-1 == bitvalcalculated)
                {
                    printf("\nCould not read Bit from inode bitmap");
                    printf("\nPartition %d,BlockGroup %d,BitOffset %d",pfsck_context->fs_info.activePartitionIdx,blockGroupNo,i);
                    return(bitvalcalculated);
                }

            if(bitvalcalculated == bitvalorig) {
                continue;
            }

            if(DATA_USED == bitvalcalculated)
                printf("\n\t=>PASS4:Fixing Data Block %d: Is incorrectly marked free in blockbitmap",
                       i+inclusive_lower_bound+pfsck_context->fs_info.ext2_super_block.s_first_data_block);
            else
                printf("\n\t=>PASS4:Fixing Data Block %d: Is incorrently marked set  in blockbitmap",
                       i+inclusive_lower_bound+pfsck_context->fs_info.ext2_super_block.s_first_data_block);

            bio_mark_dirty(pBioCalculated);
        }
    return 0;
}


int fsck_pass4(pfsck_context_t pfsck_context) {
    int            i;
    int            ret ;
    int            blockSize;
    int            nrBlockGroups;
    bio_t          bio_orginal;
    bio_t          bio_calculated;
    bio_clear_dirty(&bio_orginal);
    bio_clear_dirty(&bio_calculated);

    blockSize = (1024 << pfsck_context->fs_info.ext2_super_block.s_log_block_size);
    nrBlockGroups = pfsck_context->fs_info.ext2_super_block.s_inodes_count /
        pfsck_context->fs_info.ext2_super_block.s_inodes_per_group;


    // build up the namespace data list
    ret = fsck_pass4_build_NS_data_list(pfsck_context);
    if(ret) {
        printf("Could not build data block list for partition %d",pfsck_context->fs_info.activePartitionIdx);
        return ret;
    }


    //     for every block group
    for(i=0;i<nrBlockGroups;i++) {
        ret = bio_fs_block_read(
                                &pfsck_context->partition_info,
                                pfsck_context->fs_info.activePartitionIdx,
                                blockSize / SECTOR_SIZE,
                                pfsck_context->fs_info.pext2_group_desc[i].bg_block_bitmap,
                                &bio_orginal);
        if(ret){
            printf("Failed to verify block bitmap for group %d",i);
            continue;
        }

        //     recalculate block group data bitmap.
        bio_calculated.length = bio_orginal.length;
        ret = fsck_pass4_build_bitmap(pfsck_context,i,&bio_calculated);
        if(ret){
            printf("Failed to verify block bitmap for group %d",i);
            continue;
        }


        //     printout discripancy
        ret = fsck_print_block_bitmap_error(pfsck_context,i,&bio_calculated,&bio_orginal);
        if(ret){
            printf("Failed to verify block bitmap for group %d",i);
            continue;
        }

        //     write out the data block.
        // WRITE OUT THE BLOCK BIT MAP IF ITS DIRTY :) HE HE WE ALWAYS RECOMPUTE
        bio_calculated.disk_offset = bio_orginal.disk_offset;
        bio_calculated.length      = bio_orginal.length;

        if(!bio_is_dirty(&bio_calculated))
            continue;

        ret = bio_fs_block_write(
                                 &pfsck_context->partition_info,
                                 &bio_calculated);
        if(ret){
            printf("Failed to verify block bitmap for group %d",i);
            continue;
        }
    }
}



/***************************************************************************************/
/* PASS 3 Code and utils                                                               */
/***************************************************************************************/
int fsck_pass3(pfsck_context_t pfsck_context){
    int    ret;
    __u32  inodeNumber;
    struct ext2_inode inodetobefixed;
    int    i;
    struct node *pNode;


    /* for every indode in the name space list */
    pNode = pfsck_context->nsInodeListHeader.next;
    while(pNode) {
        for(i=0;i<pNode->next_free_element;i++) {
            /* read the inode using get */
            ret = read_ext2_get_inode(
                                      &pfsck_context->partition_info,
                                      pfsck_context->fs_info.activePartitionIdx,
                                      &pfsck_context->fs_info.ext2_super_block,
                                      pfsck_context->fs_info.pext2_group_desc,
                                      pNode->elements[i].idxNumber,
                                      &inodetobefixed);
            if(ret) {
                printf("\n Cannot read indode %d",pNode->elements[i].idxNumber);
                continue; // ok to continue here
            }


            /* if link count doesn't match put */
            if(pNode->elements[i].count == inodetobefixed.i_links_count)
                continue;

            /* fix the inode link count */
            printf("\n\t=>PASS3:Fixing inode %d linkcount %d->%d",
                   pNode->elements[i].idxNumber,
                   inodetobefixed.i_links_count,
                   pNode->elements[i].count);

            inodetobefixed.i_links_count = pNode->elements[i].count;
            /* write back the inode */
            ret = read_ext2_put_inode(
                                      &pfsck_context->partition_info,
                                      pfsck_context->fs_info.activePartitionIdx,
                                      &pfsck_context->fs_info.ext2_super_block,
                                      pfsck_context->fs_info.pext2_group_desc,
                                      pNode->elements[i].idxNumber,
                                      &inodetobefixed);
            if(ret) {
                printf("\n Cannot put back inode %d",pNode->elements[i].idxNumber);
                continue;
            }
        } // end for node
        pNode = pNode->next;
    } // end for list head

    return 0;
}


/***************************************************************************************/
/* PASS 2 Code and utils                                                               */
/***************************************************************************************/
int  getMinimumLength(struct ext2_dir_entry_2 *pDirent){
    int est = EXT2_DIR_REC_LEN(pDirent->name_len);
    return est<pDirent->rec_len?est:pDirent->rec_len;
}


int  ext2_add_lost_dirent(pfsck_context_t pfsck_context,
                          struct ext2_inode *plost_found_inode,
                          __u32 lost_found_ino,
                          __u32 found_inode_ino
                          )
{
    __u32                   temp;
    struct element          elt;
    int                     ret;
    int                     blockno=0;
    struct ext2_dir_entry_2 dirent,*pDirent,*pDirent2;
    struct ext2_inode       found_inode;
    char                    name[40],*pname;
    int                     i=38;
    bio_t                   bio;
    bio_clear_dirty(&bio);


    /* read up the found inode */
    ret = read_ext2_get_inode(
                              &pfsck_context->partition_info,
                              pfsck_context->fs_info.activePartitionIdx,
                              &pfsck_context->fs_info.ext2_super_block,
                              pfsck_context->fs_info.pext2_group_desc,
                              found_inode_ino,
                              &found_inode);
    if(ret) {
        printf("\n Cannot locate inode for lost entry %d",found_inode_ino);
        return 0;
    }




    /* build the dirent */
    temp = found_inode_ino;
    memset(name,0,sizeof(name));
    memset(&dirent,0,sizeof(dirent));
    for(;i && found_inode_ino;i--,found_inode_ino/=10)
        name[i] = "0123456789"[found_inode_ino%10];

    found_inode_ino = temp;
    name[i--] = '#';

    pname = &name[i+1];
    dirent.inode     = found_inode_ino;
    dirent.name_len  = strlen(pname);
    dirent.rec_len   = EXT2_DIR_REC_LEN(strlen(pname));
    dirent.file_type = imodetodirft(found_inode.i_mode);
    strncpy(dirent.name,pname,EXT2_NAME_LEN<strlen(pname)?EXT2_NAME_LEN:strlen(pname));

    /* add the directory entry */
    //- Search for the path component -//
    while(1) {
        ret =  read_ext2_inode_data_bio(
                                        &pfsck_context->partition_info,
                                        pfsck_context->fs_info.activePartitionIdx,
                                        &pfsck_context->fs_info.ext2_super_block,
                                        pfsck_context->fs_info.pext2_group_desc,
                                        plost_found_inode,
                                        blockno++,
                                        &bio);
        if(FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS==ret)
            return FSCK_ERR_PATH_DOES_NOT_EXIST;

        /* Search for the filename in this directory block */
        pDirent  =(struct ext2_dir_entry_2*)bio.buffer;
        printf("\n");
        while(1)
            {
                int minentrylen=0;
                /* we have searched this entire block Move to next block */
                if((char *)pDirent >= (char *)((char*)bio.buffer + bio.length))
                    break;

                /* is this entry fat enough */
                if(dirent.rec_len<=(pDirent->rec_len - getMinimumLength(pDirent))) {
                    dirent.rec_len   = pDirent->rec_len - getMinimumLength(pDirent);
                    pDirent->rec_len = getMinimumLength(pDirent);
                    pDirent2 = (struct ext2_dir_entry_2*)((char *)pDirent + pDirent->rec_len);
                    memcpy(pDirent2,&dirent,dirent.rec_len);
                    bio_mark_dirty(&bio);
                    break;
                }
                /* have we reached he last dirent for this directory */
                if(0 == pDirent->inode)
                    return FSCK_ERR_PATH_DOES_NOT_EXIST;

                /* next dirent */
                pDirent = (struct ext2_dir_entry_2*)((char *)pDirent + pDirent->rec_len);
            } // end search for dirent in the block

        /* desired changes were made */
        if(bio_is_dirty(&bio))
            break;
    }


    if(!bio_is_dirty(&bio)) {
        printf("\nCannot find free slot under lost and found for %d",found_inode_ino);
        return 0;
    }

    /* update the link count */
    plost_found_inode->i_links_count++;
    found_inode.i_links_count=1;

    ret = read_ext2_put_inode(
                              &pfsck_context->partition_info,
                              pfsck_context->fs_info.activePartitionIdx,
                              &pfsck_context->fs_info.ext2_super_block,
                              pfsck_context->fs_info.pext2_group_desc,
                              found_inode_ino,
                              &found_inode);
    if(ret) {
        printf("\n Cannot locate inode for lost entry %d",found_inode_ino);
        return 0;
    }

    ret = read_ext2_put_inode(
                              &pfsck_context->partition_info,
                              pfsck_context->fs_info.activePartitionIdx,
                              &pfsck_context->fs_info.ext2_super_block,
                              pfsck_context->fs_info.pext2_group_desc,
                              lost_found_ino,
                              plost_found_inode);
    if(ret) {
        printf("\n Cannot locate inode for lost entry %d",found_inode_ino);
        return 0;
    }

    /* write out the bio */
    ret = bio_fs_block_write(&pfsck_context->partition_info,&bio);
    ret = 0;
    if(ret) {
        printf("\nCannot write dirty block for lost+found");
        return 0;
    }

    // printDirectoryContents(&bio);


    // We have added and file to the /lost+found so we need to add this inode to
    // the list of known inodes for new passes
    elt.idxNumber = found_inode_ino;
    elt.count     = 0;
    ret = fsck_list_add_element(&pfsck_context->nsInodeListHeader,
                                &elt);
    if(ret) {
        printf("\n Failed to build Add unref node to known list %d",found_inode_ino);
        return FATAL_ERROR; // serious enough to bail out this partition completely
    }



    // If we have add an directory to the /lost+found we need to do an incremental
    // pass 1 for this directory to update its parent and self pointers.
    if(found_inode.i_mode & EXT2_S_IFDIR) {
        printf("\t=>PASS1: Trying [INCREMENTAL] pass 1 fix for DIR_INODE %d",found_inode_ino);
        ret = fsck_pass1_recurse(pfsck_context,lost_found_ino,found_inode_ino);
        if(ret) {
            printf("\nAn Directory was added to /lost+found its iterative PASS 1 has failed."
                   "Please consider running a fsck again after this run [Continuing]");
        }
        return 0;
    }

    return 0;
}



#define PASS2_SELECT_DIRS  0
#define PASS2_SELECT_FILES 1

int fsck_pass2_selective(pfsck_context_t pfsck_context,int selector) {
    char path[1024] = "/lost+found";
    struct ext2_inode lost_found_inode;
    __u32             lost_found_ino;
    struct ext2_inode found_inode;
    __u32             inodeNumber;

    int   ret;
    int   bg_idx=0;
    int   bit_idx=0;
    int   nrBlockGroups;
    int   blockSize;

    struct element elt,*pElt;
    bio_t bio;
    bio_clear_dirty(&bio);


    blockSize = (1024 << pfsck_context->fs_info.ext2_super_block.s_log_block_size);

    nrBlockGroups =
        pfsck_context->fs_info.ext2_super_block.s_inodes_count /
        pfsck_context->fs_info.ext2_super_block.s_inodes_per_group;


    //- well now inodes per group have to be < number of inodes that can fit in a block -//
    if(pfsck_context->fs_info.ext2_super_block.s_inodes_per_group > blockSize * BITS_PER_BYTE) {
        printf("\nBlock Groups to small to handle that many inodes SB=%d BG=%d",
               pfsck_context->fs_info.ext2_super_block.s_inodes_per_group,blockSize * BITS_PER_BYTE);
        printf("\n Probable superblock corruption");
        return FSCK_ERR_PARTITION_TOO_SMALL;
    }


    //- find the inode for lost+found -//
    ret = ext2_namei(
                     &pfsck_context->partition_info,
                     pfsck_context->fs_info.activePartitionIdx,
                     &pfsck_context->fs_info.ext2_super_block,
                     pfsck_context->fs_info.pext2_group_desc,
                     path,
                     &lost_found_inode,
                     &lost_found_ino);
    if(ret) {
        printf("\nCould not read entry for the lost+found directory,bailing out pass 2");
        return ret;
    }

    //- traverse each allocated inode on the disk -//
    //- for each block group -//
    for(bg_idx=0;bg_idx<nrBlockGroups;bg_idx++){
        //- read the inode bitmap -//
        ret = bio_fs_block_read(
                                &pfsck_context->partition_info,
                                pfsck_context->fs_info.activePartitionIdx,
                                blockSize / SECTOR_SIZE,
                                pfsck_context->fs_info.pext2_group_desc[bg_idx].bg_inode_bitmap,
                                &bio);
        if(ret) {
            FSCK_LOG_ERROR("Pass 0 could not inode bitmap",fsck_pass1_recurse,FSCK_ERR_IO);
            return ret;
        }

        //- for each inode in block group -//
        for(bit_idx=0;bit_idx<pfsck_context->fs_info.ext2_super_block.s_inodes_per_group;bit_idx++) {
            int bitval=-1;
            /* is this bit set */
            bitval = safeGetBit(bio.buffer,bio.length,bit_idx);
            if(-1 == bitval) {
                printf("\nCould not read Bit from inode bitmap");
                printf("\nPartition %d,BlockGroup %d,BitOffset %d",pfsck_context->fs_info.activePartitionIdx,bg_idx,bit_idx);
                return(bitval);
            }

            if(INODE_FREE==bitval)
                continue;                       // nothing to be done for free inodes atleast by this primitive fsck !!

            //OK SO THE INODE IS MARKED AllOCATED. IS IT REFERENCED IN THE NAMESPACE

            // from bit in bitmap get the inode_number
            //  inodeno = bgno * inodesperbg + bitoffset + 1;
            inodeNumber = (bg_idx *  pfsck_context->fs_info.ext2_super_block.s_inodes_per_group)+ bit_idx + 1;
            if(inodeNumber < pfsck_context->fs_info.ext2_super_block.s_first_ino && EXT2_ROOT_INO != inodeNumber)
                continue;


            elt.idxNumber = inodeNumber;
            elt.count     = 0;
            pElt          = NULL;
            ret = fsck_search_list(&pfsck_context->nsInodeListHeader,
                                   &elt,
                                   &pElt);

            // Not found in NS so its unreferenced
            // If selector is for directories we add only directories in lost+found
            // If selector is for files we add only files and others.
            if(ret) {

                //
                int   subret;
                __u16 i_mode;
                subret = read_ext2_get_inode(
                                             &pfsck_context->partition_info,
                                             pfsck_context->fs_info.activePartitionIdx,
                                             &pfsck_context->fs_info.ext2_super_block,
                                             pfsck_context->fs_info.pext2_group_desc,
                                             inodeNumber,
                                             &found_inode);
                if(subret){
                    printf("\n\t=>PASS2:Cannot find inode for the Unreferenced Inode %d",inodeNumber);
                    return subret;
                }

                i_mode = found_inode.i_mode;
                i_mode = i_mode & EXT2_S_IFMT;

                if(PASS2_SELECT_DIRS == selector && EXT2_S_IFDIR != i_mode)
                    continue;
                if(PASS2_SELECT_FILES == selector && EXT2_S_IFDIR == i_mode)
                    continue;
                //


                printf("\n\t=>PASS2:Fixing Unreferenced Inode Found %d",inodeNumber);
                printf("\n\t  Location Partition %d,BlockGroup %d,BitOffset %d",
                       pfsck_context->fs_info.activePartitionIdx,
                       bg_idx,
                       bit_idx);

                ext2_add_lost_dirent(pfsck_context,
                                     &lost_found_inode,
                                     lost_found_ino,
                                     inodeNumber
                                     );
                printf("\t  Added to lost+found");
            }

        } // End every inode in block group

    } // End every block group
    return 0;
}


int fsck_pass2(pfsck_context_t pfsck_context) {
    int ret;
    ret = fsck_pass2_selective(pfsck_context,PASS2_SELECT_DIRS);
    if(ret) {
        printf("\n Cannot add directories to to lost+found");
        return ret;
    }

    ret = fsck_pass2_selective(pfsck_context,PASS2_SELECT_FILES);
    if(ret) {
        printf("\n Cannot add file to to lost+found");
        return ret;
    }
    return 0;
}




/***************************************************************************************/
/* PASS 1 Code and utils                                                               */
/***************************************************************************************/
int fsck_fix_self_parent_inodes(
                                pfsck_partition_info_t pfsck_partition_info,
                                pbio_t pbio,
                                __u32  parent_inode_nr,
                                __u32  self_inode_nr) {
    struct ext2_dir_entry_2 *pDirent;
    pDirent = (struct ext2_dir_entry_2 *) pbio->buffer;
    if(1 != pDirent->name_len || '.' != pDirent->name[0] || self_inode_nr != pDirent->inode)
        {
            printf("\n\t=>PASS1:Fixing Inode %d . %d->%d ",self_inode_nr,pDirent->inode,self_inode_nr);
            pDirent->name_len = 1;
            pDirent->name[0]  = '.';
            pDirent->inode = self_inode_nr;
            bio_mark_dirty(pbio);
        }
    pDirent = (struct ext2_dir_entry_2 *)((char*)pDirent + pDirent->rec_len);
    if(2 != pDirent->name_len || '.' != pDirent->name[0] || '.' != pDirent->name[0] || parent_inode_nr != pDirent->inode)
        {
            printf("\n\t=>PASS1:Fixing Inode %d .. %d->%d ",self_inode_nr,pDirent->inode,parent_inode_nr);
            pDirent->name_len = 2;
            pDirent->name[0]  = '.';
            pDirent->name[1]  = '.';
            pDirent->inode    = parent_inode_nr;
            bio_mark_dirty(pbio);
        }

    if(bio_is_dirty(pbio))
        return bio_fs_block_write(pfsck_partition_info,pbio);
    else
        return 0;
    return 0;
}



int fsck_pass1_recurse(pfsck_context_t pfsck_context,
                       __u32           parent_inode_nr,
                       __u32           self_inode_nr) {
    int ret;
    struct ext2_dir_entry_2 *pDirent;
    struct ext2_inode       self_inode;
    bio_t                   bio;
    struct element          elt;
    int                     blockNo=0;
    bio_clear_dirty(&bio);

    /* check and fix our . and .. entries */
    ret = read_ext2_get_inode(
                              &pfsck_context->partition_info,
                              pfsck_context->fs_info.activePartitionIdx,
                              &pfsck_context->fs_info.ext2_super_block,
                              pfsck_context->fs_info.pext2_group_desc,
                              self_inode_nr,
                              &self_inode);
    if(ret) {
        FSCK_LOG_ERROR("Could not read root inode", read_ext2_get_inode,ret);
        return ret;
    }
    if(!(self_inode.i_mode & EXT2_S_IFDIR)){
        FSCK_LOG_ERROR("Pass 0 attempting to read non directory",fsck_pass1_recurse,FATAL_ERROR);
        return FATAL_ERROR;
    }

    /* Read the block 0 of this directory */
    ret =  read_ext2_inode_data_bio(
                                    &pfsck_context->partition_info,
                                    pfsck_context->fs_info.activePartitionIdx,
                                    &pfsck_context->fs_info.ext2_super_block,
                                    pfsck_context->fs_info.pext2_group_desc,
                                    &self_inode,
                                    blockNo++,
                                    &bio);
    if(ret) {
        FSCK_LOG_ERROR("Pass 0 could not read dir data block",fsck_pass1_recurse,FSCK_ERR_IO);
        return ret;
    }

    /* check for sanity of . and .. entries */
    ret = fsck_fix_self_parent_inodes(&pfsck_context->partition_info,
                                      &bio,
                                      parent_inode_nr,
                                      self_inode_nr);
    if(ret) {
        printf("\n Pass0 Couldn't fix inode parent %d & child %d",parent_inode_nr,self_inode_nr);
        return ret;
    }

    /* after fixing we have two dirent for the parent and the child */
    elt.idxNumber = parent_inode_nr;
    elt.count     = 0;
    ret = fsck_list_add_element(&pfsck_context->nsInodeListHeader,
                                &elt);
    if(ret) {
        printf("\n Failed to build NSinode list at inode %d",self_inode_nr);
        return FATAL_ERROR; // serious enough to bail out this partition completely
    }
    elt.idxNumber = self_inode_nr;
    elt.count     = 0;
    ret = fsck_list_add_element(&pfsck_context->nsInodeListHeader,
                                &elt);
    if(ret) {
        printf("\n Failed to build NSinode list at inode %d",self_inode_nr);
        return FATAL_ERROR; // serious enough to bail out this partition completely
    }


    /* initiate the same checks for all our children directories */
    /* get the number of blocks allocated for our directory */
    pDirent  =(struct ext2_dir_entry_2*)bio.buffer;
    pDirent  =(struct ext2_dir_entry_2*)((char *)pDirent + pDirent->rec_len); // skip .
    pDirent  =(struct ext2_dir_entry_2*)((char *)pDirent + pDirent->rec_len); // skip ..

    // until we are done reading all blocks
    do{
        // until we fall off the block //
        while(1)
            {
                char *p1,*p2;
                elt.idxNumber = 0;
                elt.count     = 0;

                p1 = (char*)pDirent;
                p2 = (char*)((char*)bio.buffer + bio.length);
                if(p1>=p2) break;



                // have we reached he last dirent for this directory //
                if(0 == pDirent->inode)
                    return 0;

                // Also build up the ns inode list required for the next passes.
                //print_dirent(pDirent);
                elt.idxNumber = pDirent->inode;
                elt.count     = 0;
                ret = fsck_list_add_element(&pfsck_context->nsInodeListHeader,
                                            &elt);
                if(ret) {
                    printf("\n Failed to build NSinode list at inode %d",self_inode_nr);
                    return FATAL_ERROR; // serious enough to bail out this partition completely
                }

                if(pDirent->file_type == EXT2_FT_DIR) {
                    ret = fsck_pass1_recurse(pfsck_context,self_inode_nr,pDirent->inode);
                    if(ret) {
                        printf("\n Pass0 Couldn't fix inode %d",self_inode_nr);
                        ret = 0; // ignore
                    }
                }
                // next dirent //
                pDirent = (struct ext2_dir_entry_2*)((char *)pDirent + pDirent->rec_len);
            } // end search for dirent in the block

        ret =  read_ext2_inode_data_bio(
                                        &pfsck_context->partition_info,
                                        pfsck_context->fs_info.activePartitionIdx,
                                        &pfsck_context->fs_info.ext2_super_block,
                                        pfsck_context->fs_info.pext2_group_desc,
                                        &self_inode,
                                        blockNo++,
                                        &bio);
        pDirent  = (struct ext2_dir_entry_2*)bio.buffer;
    }while(ret==0);


    /* done reading all blocks */
    if(ret==FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS)
        return 0;
    else
        return ret;
}




int fsck_pass1(pfsck_context_t pfsck_context) {
    int ret;

    /* Recursively traverse the */
    ret = fsck_pass1_recurse(pfsck_context,EXT2_ROOT_INO,EXT2_ROOT_INO);
    return ret;
}






/* Partition table should be pre-read */
int prepareContextForPartition(fsck_context_t *pfsck_context,int partitionIdx){
    int ret;
    // Initialize book-Keeping info:
    fsck_list_init_header(&pfsck_context->nsInodeListHeader);
    fsck_list_init_header(&pfsck_context->nsDataListHeader);
    pfsck_context->fs_info.activePartitionIdx = partitionIdx;

    // read the super block of the partition //
    ret = read_ext2_superblock(
                               &pfsck_context->partition_info,
                               partitionIdx,
                               &pfsck_context->fs_info.ext2_super_block);
    if(ret) {
        FSCK_LOG_ERROR("Read superblock failed",read_ext2_superblock,ret);
        return ret;
    }

    if(EXT2_SUPER_MAGIC == pfsck_context->fs_info.ext2_super_block.s_magic)
        printf("\n\n\nPartition%d:Superblock Magic=%x",partitionIdx+1,pfsck_context->fs_info.ext2_super_block.s_magic);
    else
        FSCK_LOG_ERROR("Superblock BAD MAGIC Number",read_ext2_superblock,EXT2_SUPER_MAGIC);


    // read the group decriptors //
    ret = read_ext2_block_descriptors(
                                      &pfsck_context->partition_info,
                                      partitionIdx,
                                      &pfsck_context->fs_info.ext2_super_block,
                                      &pfsck_context->fs_info.pext2_group_desc);
    if(ret) {
        FSCK_LOG_ERROR("Cannot read group descriptors Skipping",read_ext2_block_descriptors,ret);
        return ret;
    }
    return 0;
}


int cleanupContextOfPartition(fsck_context_t *pfsck_context,int partitionIdx) {
    int ret;
    // cleanup-book-keeping information:
    ret =  fsck_free_list(&pfsck_context->nsDataListHeader);
    if(ret)
        FSCK_LOG_ERROR("Warn: failed to cleanup context",fsck_free_list,0);
    ret =  fsck_free_list(&pfsck_context->nsDataListHeader);
    if(ret)
        FSCK_LOG_ERROR("Warn: failed to cleanup context",fsck_free_list,0);
    return 0;
}



/* Dumb macro to save editor space */
#define PerformFsck(pass)  {                                            \
        printf("\nSTART FSCK Pass"#pass" on Partiton %d",partitionIdx+1); \
        ret = fsck_pass##pass(&fsck_context);                           \
            if(ret)                                                     \
                printf("\nEND   FSCK Pass"#pass" on Parition %d [FAILURE]\n",partitionIdx+1); \
            else                                                        \
                printf("\nEND   FSCK Pass"#pass" on Parition %d [SUCCESS]\n",partitionIdx+1); \
            if(FATAL_ERROR == ret) {                                    \
                printf("\nABORT FSCK Pass"#pass" failed fataly on parition %d abandoning partition\n",partitionIdx+1); \
                continue;                                               \
            }                                                           \
    }


int main(int argc,char **argv) {
int            ret;
int            partitionIdx;
fsck_context_t fsck_context;

if(argc != 2) {
print_usage();
exit(-1);
}


// Read the partitionTable //
ret = read_partiontable(&fsck_context.partition_info,argv[1]);
if(ret)
    FSCK_LOG_ERROR_AND_EXIT("Could not read Partition Table",prepare_fsck,ret);




// For each partition found to be ext2 perform fsck //
for(partitionIdx=0;partitionIdx<fsck_context.partition_info.partitions_nr;partitionIdx++) {
if(fsck_context.partition_info.partition_table[partitionIdx].sys_ind != LINUX_EXT2_PARTITION)
    continue;

/* prepare the context for this partition */
ret =  prepareContextForPartition(&fsck_context,partitionIdx);
if(ret) {
FSCK_LOG_ERROR("failed to prepare context for partition",prepareContextForPartition,ret);
continue;
}

printf("\n");
PerformFsck(1);
// ret = fsck_dump_list(&fsck_context.nsInodeListHeader);
PerformFsck(2);
PerformFsck(3);
PerformFsck(4);
//ret = fsck_dump_list(&fsck_context.nsDataListHeader);
/* cleanup the context for this partition */
ret =  cleanupContextOfPartition(&fsck_context,partitionIdx);
if(ret) {
FSCK_LOG_ERROR("failed to prepare context for partition",prepareContextForPartition,ret);
continue;
}
}
printf("\n");
close(fsck_context.partition_info.fd);
return ret;
}

