#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <genhd.h>
#include <partition_manager.h>
#include <fsck_errors.h>
#include <ext2_fs.h>
#include <math.h>
#include <ext2_structures.h>
#include <bio.h>
#include <string.h>

__u8    imodetodirft(__u16   imode)
{
    imode = imode & EXT2_S_IFMT;
    if(EXT2_S_IFSOCK == imode)
        return   EXT2_FT_SOCK;
    if(EXT2_S_IFLNK == imode)
        return   EXT2_FT_SYMLINK;
    if(EXT2_S_IFREG == imode)
        return   EXT2_FT_REG_FILE;
    if(EXT2_S_IFBLK == imode)
        return   EXT2_FT_BLKDEV;
    if(EXT2_S_IFDIR == imode)
        return   EXT2_FT_DIR;
    if(EXT2_S_IFCHR == imode)
        return   EXT2_FT_CHRDEV;
    if(EXT2_S_IFIFO == imode)
        return   EXT2_FT_FIFO;
    return  EXT2_FT_UNKNOWN;
}




/*********************************************************************************************/
/* read_ext2_superblock                                                                      */
/* Reads a ext2 superblock off a requested partitions                                        */
/*                                                                                           */
/* In:                                                                                       */
/*  pfsck_partition_info_t                                                                   */
/* A pre-initialized partition information structure containing the whereabouts of           */
/* the partitions on the disk.                                                               */
/*                                                                                           */
/* Out:                                                                                      */
/* A read superblock of size of 1024 if everything matches.                                  */
/*                                                                                           */
/* Errors:                                                                                   */
/* If the partition does not match canonical representation of EXT2 appropriate error        */
/* is returned.                                                                              */
/*********************************************************************************************/
int read_ext2_superblock(
                         IN  pfsck_partition_info_t   pfsck_partition_info,
                         IN  int                      partition_no,
                         OUT struct ext2_super_block *pExt2SuperBlock) {
    struct partition *ppartition;
    int ret;

    if(partition_no < 0 || partition_no >= pfsck_partition_info->partitions_nr) {
        FSCK_LOG_ERROR("No such partition",read_ext2_superblock,FSCK_ERR_NO_SUCH_PARTITION);
        return FSCK_ERR_NO_SUCH_PARTITION;
    }

    ppartition =  &(pfsck_partition_info->partition_table[partition_no]);

    if(ppartition->start_sect+2 >
       ppartition->start_sect+ppartition->nr_sects) {
        FSCK_LOG_ERROR("Partition too small",read_ext2_superblock,FSCK_ERR_PARTITION_TOO_SMALL);
        return FSCK_ERR_PARTITION_TOO_SMALL;
    }

    if(LINUX_EXT2_PARTITION != ppartition->sys_ind) {
        FSCK_LOG_ERROR("Partition had no native Linux FS",read_ext2_superblock,FSCK_ERR_NOT_LINUX_NATIVE_FS);
    }


    /* Read into 1024 bytes of the partition */
    ret = readPartitionSectorExtent(pfsck_partition_info,partition_no,2,2,pfsck_partition_info->buffer);
    if(ret) {
        FSCK_LOG_ERROR("",readSector,errno);
        return(errno);
    }

    memcpy(pExt2SuperBlock,pfsck_partition_info->buffer,sizeof(*pExt2SuperBlock));
    return 0;
}



/*********************************************************************************/
/* read_ext2_block_descriptors                                                   */
/*       Reads the block group descriptors for and EXT3 filesystem.              */
/*  Input                                                                        */
/*       pfsck_partition_info                                                    */
/*        Pre-initialized partitiond table.                                      */
/*       partition_no                                                            */
/*        The partion number whose group descriptors are to be read              */
/*       pExt2SuperBlock                                                         */
/*        A pre-read Ext Superblock structure for this partition                 */
/*  Output                                                                       */
/*       ppExt2GroupDesc                                                         */
/*        Pointer to pointer that would get the location of allocated            */
/*        group descriptors. This memory should be released using free           */
/*        by the caller                                                          */
/*  Return                                                                       */
/*       0 on error                                                              */
/*       erro of fsckError values is something goes wrong.                       */
/*********************************************************************************/
int read_ext2_block_descriptors(
                                IN  pfsck_partition_info_t   pfsck_partition_info,
                                IN  int                      partition_no,
                                IN  struct ext2_super_block *pExt2SuperBlock,
                                OUT struct ext2_group_desc **ppExt2GroupDesc)
{
    char *buffer;
    int blockSize;
    int ret;
    blockSize = 1024 << pExt2SuperBlock->s_log_block_size;

    buffer = (char *)malloc(blockSize);
    if(NULL == buffer) {
        FSCK_LOG_ERROR("No memory for group descriptor",read_ext2_block_descriptors,errno);
        return errno;
    }
    memset(buffer,0,blockSize);

    /* Read the group descriptor block for this partition*/
    ret =  readPartitionSectorExtent(
                                     pfsck_partition_info,
                                     partition_no,
                                     4,
                                     blockSize/SECTOR_SIZE,
                                     buffer);
    if(ret) {
        FSCK_LOG_ERROR("Failed to read group desc blocks",readPartitionSectorExtent,errno);
        return ret;
    }

    *ppExt2GroupDesc = (struct ext2_group_desc *) buffer;
    return 0;
}


__u32 fsck_power(__u32 x,int y) {
    __u32 ans=1;
    while(y--) {ans *= x;}
    return ans;
}

int  getIndirectPhysicalBlockNumber(
                                    IN  pfsck_partition_info_t   pfsck_partition_info,
                                    IN  int                      partition_no,
                                    IN  struct ext2_super_block *pExt2SuperBlock,
                                    IN  struct ext2_group_desc  *pExt2GroupDesc,
                                    IN  struct ext2_inode       *pExt2Inode,
                                    IN  __u32                    rootBlockNumber,
                                    IN  int                      IndirectionLevel,
                                    IN  long                     relLogicalBlockNo,
                                    IN  struct fsck_list_header *pnsDataListHeader,
                                    IN  __u32                   *physicalBlockNo
                                    )
{
    int i;
    int blockSize = 1024 << pExt2SuperBlock->s_log_block_size;
    int SectorsPerBlock = blockSize / SECTOR_SIZE;
    int nrBlocks  = pExt2Inode->i_blocks / SectorsPerBlock;
    __u32 addrPerBlock = blockSize / sizeof(__u32);
    __u32 handledAtRootEntry =  fsck_power(addrPerBlock,IndirectionLevel-1);
    int ret;

    for(i=0;i<IndirectionLevel;i++) {
        ret = readPartitionBlockExtent(
                                       pfsck_partition_info,
                                       partition_no,
                                       (1024 << pExt2SuperBlock->s_log_block_size) / SECTOR_SIZE,
                                       rootBlockNumber,
                                       1,
                                       pfsck_partition_info->buffer);
        if(ret) {
            FSCK_LOG_ERROR("Could not read idirect block",readPartitionBlockExtent,ret);
            return ret;
        }

        /* Insert the indirect blocks owned by the inode FS */
        if(pnsDataListHeader) {
            struct element elt;
            elt.idxNumber = rootBlockNumber;
            elt.count     = 0;
            ret = fsck_list_add_element(
                                        pnsDataListHeader,
                                        &elt);
            if(ret) {
                printf("\n Failed to duild NSdatablock for indirect block %d",elt.idxNumber);
                return ret; // serious enough to bail out this partition completely
            }
        }


        /* index of second root */
        rootBlockNumber = relLogicalBlockNo / handledAtRootEntry;
        rootBlockNumber = rootBlockNumber % addrPerBlock;
        /* Second root if any */
        rootBlockNumber =  ((__u32 *)pfsck_partition_info->buffer)[rootBlockNumber];
        handledAtRootEntry /= addrPerBlock;
    }

    *physicalBlockNo =   ((__u32 *)pfsck_partition_info->buffer)[relLogicalBlockNo%addrPerBlock];
    return 0;
}




int read_ext2_LCN_TO_VCN(
                         IN  pfsck_partition_info_t   pfsck_partition_info,
                         IN  int                      partition_no,
                         IN  struct ext2_super_block *pExt2SuperBlock,
                         IN  struct ext2_group_desc  *pExt2GroupDesc,
                         IN  struct ext2_inode       *pExt2Inode,
                         IN  long                     logicalBlockNo,
                         IN  struct fsck_list_header *pnsDataListHeader,
                         OUT __u32                   *pVCN)
{
    __u32 physicalBlockNo;
    int   blockSize = 1024 << pExt2SuperBlock->s_log_block_size;;
    int   addressesPerBlock =  blockSize/sizeof(__u32);
    int   ret;
    int   MaxDirect,MaxInDirect,MaxDoubleInDirect,MaxTripleIndirect;
    int   SectorsPerBlock = blockSize / SECTOR_SIZE;
    int   nrBlocks;



    if(pExt2Inode->i_mode & EXT2_S_IFLNK  && pExt2Inode->i_size <= 60)
        return(FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS);


    /* Sanity checks */
    nrBlocks        = pExt2Inode->i_blocks * SECTOR_SIZE / blockSize;
    if(logicalBlockNo >= nrBlocks) {
        //FSCK_LOG_ERROR("No such block for file",getPhysicalBlockNumber,FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS);
        return(FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS);
    }


    /* Calculations for the final physical block number */
    MaxDirect         = EXT2_NDIR_BLOCKS;
    MaxInDirect       = MaxDirect + addressesPerBlock;
    MaxDoubleInDirect = MaxInDirect + (addressesPerBlock * addressesPerBlock);
    MaxTripleIndirect = MaxDoubleInDirect +  (addressesPerBlock * addressesPerBlock * addressesPerBlock);

    if(logicalBlockNo >= MaxDoubleInDirect || logicalBlockNo < 0){
        FSCK_LOG_ERROR("Request Logical Block cannot be mapped to physical Block",
                       read_ext2_inode_data_block,
                       FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS);
        return(FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS);
    }
    else if(logicalBlockNo>=MaxDoubleInDirect) {
        /* Triple Indirect Addressing */
        physicalBlockNo = 0;
        ret = getIndirectPhysicalBlockNumber(
                                             pfsck_partition_info,
                                             partition_no,
                                             pExt2SuperBlock,
                                             pExt2GroupDesc,
                                             pExt2Inode,
                                             pExt2Inode->i_block[EXT2_TIND_BLOCK],
                                             3,                /* single indirect */
                                             logicalBlockNo - MaxDoubleInDirect,
                                             pnsDataListHeader,
                                             &physicalBlockNo);
        if(ret) {
            FSCK_LOG_ERROR("Could not get physical block number 4 single direct block",getPhysicalBlockNumber,ret);
            return ret;
        }
    }
    else if(logicalBlockNo>=MaxInDirect) {
        /* Double Indirect Addressing */
        physicalBlockNo = 0;
        ret = getIndirectPhysicalBlockNumber(
                                             pfsck_partition_info,
                                             partition_no,
                                             pExt2SuperBlock,
                                             pExt2GroupDesc,
                                             pExt2Inode,
                                             pExt2Inode->i_block[EXT2_DIND_BLOCK],
                                             2,                /* single indirect */
                                             logicalBlockNo - MaxInDirect,
                                             pnsDataListHeader,
                                             &physicalBlockNo);
        if(ret) {
            FSCK_LOG_ERROR("Could not get physical block number 4 single direct block",getPhysicalBlockNumber,ret);
            return ret;
        }
    }
    else if(logicalBlockNo>=MaxDirect) {
        /* Single Indirect Mapping */
        physicalBlockNo = 0;
        ret = getIndirectPhysicalBlockNumber(
                                             pfsck_partition_info,
                                             partition_no,
                                             pExt2SuperBlock,
                                             pExt2GroupDesc,
                                             pExt2Inode,
                                             pExt2Inode->i_block[EXT2_IND_BLOCK],
                                             1,                /* single indirect */
                                             logicalBlockNo - MaxDirect,
                                             pnsDataListHeader,
                                             &physicalBlockNo);
        if(ret) {
            FSCK_LOG_ERROR("Could not get physical block number 4 single direct block",getPhysicalBlockNumber,ret);
            return ret;
        }

    }
    else {
        /* Direct Addressing */
        physicalBlockNo = pExt2Inode->i_block[logicalBlockNo];
    }

    /* Read the block and return it */
    *pVCN = physicalBlockNo;
    return 0;
}


int read_ext2_inode_data_bio(
                             IN  pfsck_partition_info_t   pfsck_partition_info,
                             IN  int                      partition_no,
                             IN  struct ext2_super_block *pExt2SuperBlock,
                             IN  struct ext2_group_desc  *pExt2GroupDesc,
                             IN  struct ext2_inode       *pExt2Inode,
                             IN  long                     logicalBlockNo,
                             OUT pbio_t                   pbio)
{
    __u32 physicalBlockNo;
    int   blockSize = 1024 << pExt2SuperBlock->s_log_block_size;;
    int   addressesPerBlock =  blockSize/sizeof(__u32);
    int   ret;
    int   MaxDirect,MaxInDirect,MaxDoubleInDirect,MaxTripleIndirect;
    int   SectorsPerBlock = blockSize / SECTOR_SIZE;
    int   nrBlocks;



    if(pExt2Inode->i_mode & EXT2_S_IFLNK  && pExt2Inode->i_size <= 60)
        return(FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS);


    /* Sanity checks */
    nrBlocks        = pExt2Inode->i_blocks * SECTOR_SIZE / blockSize;
    if(logicalBlockNo >= nrBlocks) {
        //FSCK_LOG_ERROR("No such block for file",getPhysicalBlockNumber,FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS);
        return(FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS);
    }


    /* Calculations for the final physical block number */
    MaxDirect         = EXT2_NDIR_BLOCKS;
    MaxInDirect       = MaxDirect + addressesPerBlock;
    MaxDoubleInDirect = MaxInDirect + (addressesPerBlock * addressesPerBlock);
    MaxTripleIndirect = MaxDoubleInDirect +  (addressesPerBlock * addressesPerBlock * addressesPerBlock);

    if(logicalBlockNo >= MaxDoubleInDirect || logicalBlockNo < 0){
        FSCK_LOG_ERROR("Request Logical Block cannot be mapped to physical Block",
                       read_ext2_inode_data_block,
                       FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS);
        return(FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS);
    }
    else if(logicalBlockNo>=MaxDoubleInDirect) {
        /* Triple Indirect Addressing */
        physicalBlockNo = 0;
        ret = getIndirectPhysicalBlockNumber(
                                             pfsck_partition_info,
                                             partition_no,
                                             pExt2SuperBlock,
                                             pExt2GroupDesc,
                                             pExt2Inode,
                                             pExt2Inode->i_block[EXT2_TIND_BLOCK],
                                             3,                /* single indirect */
                                             logicalBlockNo - MaxDoubleInDirect,
                                             NULL,
                                             &physicalBlockNo);
        if(ret) {
            FSCK_LOG_ERROR("Could not get physical block number 4 single direct block",getPhysicalBlockNumber,ret);
            return ret;
        }
    }
    else if(logicalBlockNo>=MaxInDirect) {
        /* Double Indirect Addressing */
        physicalBlockNo = 0;
        ret = getIndirectPhysicalBlockNumber(
                                             pfsck_partition_info,
                                             partition_no,
                                             pExt2SuperBlock,
                                             pExt2GroupDesc,
                                             pExt2Inode,
                                             pExt2Inode->i_block[EXT2_DIND_BLOCK],
                                             2,                /* single indirect */
                                             logicalBlockNo - MaxInDirect,
                                             NULL,
                                             &physicalBlockNo);
        if(ret) {
            FSCK_LOG_ERROR("Could not get physical block number 4 single direct block",getPhysicalBlockNumber,ret);
            return ret;
        }
    }
    else if(logicalBlockNo>=MaxDirect) {
        /* Single Indirect Mapping */
        physicalBlockNo = 0;
        ret = getIndirectPhysicalBlockNumber(
                                             pfsck_partition_info,
                                             partition_no,
                                             pExt2SuperBlock,
                                             pExt2GroupDesc,
                                             pExt2Inode,
                                             pExt2Inode->i_block[EXT2_IND_BLOCK],
                                             1,                /* single indirect */
                                             logicalBlockNo - MaxDirect,
                                             NULL,
                                             &physicalBlockNo);
        if(ret) {
            FSCK_LOG_ERROR("Could not get physical block number 4 single direct block",getPhysicalBlockNumber,ret);
            return ret;
        }

    }
    else {
        /* Direct Addressing */
        physicalBlockNo = pExt2Inode->i_block[logicalBlockNo];
    }


    /* Read the block and return it */
    ret = bio_fs_block_read(
                            pfsck_partition_info,
                            partition_no,
                            (1024 << pExt2SuperBlock->s_log_block_size) / SECTOR_SIZE,
                            physicalBlockNo,
                            pbio);
    if(ret) {
        FSCK_LOG_ERROR("Could not read physical block",readPartitionBlockExtent,ret);
        return ret;
    }
    return 0;
}





int print_dirent(struct ext2_dir_entry_2 *pDirent) {
    printf("\n");
    int i;
    printf("0x%x\t%d\t",pDirent->file_type,pDirent->inode);
    for(i=0;i<pDirent->name_len;i++)
        printf("%c",pDirent->name[i]);

}



int printDirectoryContents(pbio_t pbio) {
    int i;
    struct ext2_dir_entry_2 *pDirent;
    pDirent  =(struct ext2_dir_entry_2*)pbio->buffer;

    printf("\n");
    while(1)
        {
            if(0 == pDirent->inode || (char *)pDirent > (char *)((char*)pbio->buffer + pbio->length - 1))
                break;
            print_dirent(pDirent);
            /* next dirent */
            pDirent = (struct ext2_dir_entry_2*)((char *)pDirent + pDirent->rec_len);
        }
}


int read_ext2_dumpDirectoryContents(
                                    IN  pfsck_partition_info_t   pfsck_partition_info,
                                    IN  int                      partition_no,
                                    IN  struct ext2_super_block *pExt2SuperBlock,
                                    IN  struct ext2_group_desc  *pExt2GroupDesc,
                                    IN  struct ext2_inode       *pExt2DirInode)
{
    int       blockno=0;
    int       ret;
    bio_t     bio;
    bio_clear_dirty(&bio);

    if(!(pExt2DirInode->i_mode & EXT2_S_IFDIR)) {
        FSCK_LOG_ERROR("Inode doesn't point to an directory",read_ext2_dumpDirectoryContents,FSCK_ERR_INVALID_OPERATION);
        return FSCK_ERR_INVALID_OPERATION;
    }

    while(1) {
        ret =  read_ext2_inode_data_bio(
                                        pfsck_partition_info,
                                        partition_no,
                                        pExt2SuperBlock,
                                        pExt2GroupDesc,
                                        pExt2DirInode,
                                        blockno++,
                                        &bio);
        if(ret==FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS)
            return 0;

        printDirectoryContents(&bio);
    }

    return 0;
}


int read_ext2_put_inode(
                        IN pfsck_partition_info_t    pfsck_partition_info,
                        IN int                       partition_no,
                        IN struct ext2_super_block  *pExt2SuperBlock,
                        IN struct ext2_group_desc   *pExt2GroupDesc,
                        IN unsigned long             inodenumber,
                        IN struct ext2_inode        *pExt2Inode)
{
    int blockGroupNo;
    int offset;
    int nrBlockGroups;
    int ret;
    int inodeTableRelativeBlock;
    int inodeTableRelativeBlockOffset;
    int inodesPerblock;

    /* which block group the inode falls into */
    blockGroupNo = (inodenumber - 1) / pExt2SuperBlock->s_inodes_per_group;
    offset       = (inodenumber - 1) % pExt2SuperBlock->s_inodes_per_group;

    /* Sanity checks */
    nrBlockGroups = pExt2SuperBlock->s_inodes_count / pExt2SuperBlock->s_inodes_per_group;

    if(blockGroupNo >= nrBlockGroups) {
        FSCK_LOG_ERROR("Non Existing Inode",read_ext2_get_inode,FSCK_ERR_CANNOT_MAP_INODE);
        return FSCK_ERR_CANNOT_MAP_INODE;
    }


    /* Read the inode table from the block group */
    inodesPerblock                   = (1024 << pExt2SuperBlock->s_log_block_size) / pExt2SuperBlock->s_inode_size;
    inodeTableRelativeBlock          = offset / (inodesPerblock);
    inodeTableRelativeBlockOffset    = offset % (inodesPerblock);

    ret = readPartitionBlockExtent(
                                   pfsck_partition_info,
                                   partition_no,
                                   (1024 << pExt2SuperBlock->s_log_block_size) / SECTOR_SIZE,
                                   pExt2GroupDesc[blockGroupNo].bg_inode_table+inodeTableRelativeBlock,
                                   1,
                                   pfsck_partition_info->buffer);
    if(ret) {
        FSCK_LOG_ERROR("Could not read extent for inode table",readPartitionBlockExtent,ret);
        return ret;
    }

    /* Return the inode structure to the client */
    memcpy(pfsck_partition_info->buffer + pExt2SuperBlock->s_inode_size * inodeTableRelativeBlockOffset,
           pExt2Inode,
           sizeof(*pExt2Inode));


    ret = writePartitionBlockExtent(
                                    pfsck_partition_info,
                                    partition_no,
                                    (1024 << pExt2SuperBlock->s_log_block_size) / SECTOR_SIZE,
                                    pExt2GroupDesc[blockGroupNo].bg_inode_table+inodeTableRelativeBlock,
                                    1,
                                    pfsck_partition_info->buffer);
    if(ret) {
        FSCK_LOG_ERROR("Could not write back for inode",readPartitionBlockExtent,ret);
        return ret;
    }
    return 0;
}







int read_ext2_get_inode(
                        IN pfsck_partition_info_t    pfsck_partition_info,
                        IN int                       partition_no,
                        IN struct ext2_super_block  *pExt2SuperBlock,
                        IN struct ext2_group_desc   *pExt2GroupDesc,
                        IN unsigned long             inodenumber,
                        OUT struct ext2_inode       *pExt2Inode)
{
    int blockGroupNo;
    int offset;
    int nrBlockGroups;
    int ret;
    int inodeTableRelativeBlock;
    int inodeTableRelativeBlockOffset;
    int inodesPerblock;

    /* which block group the inode falls into */
    blockGroupNo = (inodenumber - 1) / pExt2SuperBlock->s_inodes_per_group;
    offset       = (inodenumber - 1) % pExt2SuperBlock->s_inodes_per_group;

    /* Sanity checks */
    nrBlockGroups = pExt2SuperBlock->s_inodes_count / pExt2SuperBlock->s_inodes_per_group;

    if(blockGroupNo >= nrBlockGroups) {
        FSCK_LOG_ERROR("Non Existing Inode",read_ext2_get_inode,FSCK_ERR_CANNOT_MAP_INODE);
        return FSCK_ERR_CANNOT_MAP_INODE;
    }


    /* Read the inode table from the block group */
    inodesPerblock                   = (1024 << pExt2SuperBlock->s_log_block_size) / pExt2SuperBlock->s_inode_size;
    inodeTableRelativeBlock          = offset / (inodesPerblock);
    inodeTableRelativeBlockOffset    = offset % (inodesPerblock);

    ret = readPartitionBlockExtent(
                                   pfsck_partition_info,
                                   partition_no,
                                   (1024 << pExt2SuperBlock->s_log_block_size) / SECTOR_SIZE,
                                   pExt2GroupDesc[blockGroupNo].bg_inode_table+inodeTableRelativeBlock,
                                   1,
                                   pfsck_partition_info->buffer);
    if(ret) {
        FSCK_LOG_ERROR("Could not read extent for inode table",readPartitionBlockExtent,ret);
        return ret;
    }

    /* Return the inode structure to the client */
    memcpy(pExt2Inode,
           pfsck_partition_info->buffer + pExt2SuperBlock->s_inode_size * inodeTableRelativeBlockOffset,
           sizeof(*pExt2Inode));

    return 0;
}


int getBit(char *bitmap,int bitNumber){
    return( bitmap[bitNumber/8] & (0x01 << bitNumber%8) );
}

int isBlockUsed(
                IN pfsck_partition_info_t    pfsck_partition_info,
                IN int                       partition_no,
                IN struct ext2_super_block  *pExt2SuperBlock,
                IN struct ext2_group_desc   *pExt2GroupDesc,
                IN unsigned long             blockNo
                )
{
}

int isInodeUsed(
                IN pfsck_partition_info_t    pfsck_partition_info,
                IN int                       partition_no,
                IN struct ext2_super_block  *pExt2SuperBlock,
                IN struct ext2_group_desc   *pExt2GroupDesc,
                IN unsigned long             inodenumber
                )
{
    int blockGroupNo;
    int offset;
    int nrBlockGroups;
    int ret;

    /* which block group the inode falls into */
    blockGroupNo = (inodenumber - 1) / pExt2SuperBlock->s_inodes_per_group;
    offset       = (inodenumber - 1) % pExt2SuperBlock->s_inodes_per_group;

    /* Sanity checks */
    nrBlockGroups = pExt2SuperBlock->s_inodes_count / pExt2SuperBlock->s_inodes_per_group;

    if(blockGroupNo >= nrBlockGroups) {
        FSCK_LOG_ERROR("Non Existing Inode",read_ext2_get_inode,FSCK_ERR_CANNOT_MAP_INODE);
        return FSCK_ERR_CANNOT_MAP_INODE;
    }

    /* Read the inode bitmap for the block group */
    ret = readPartitionBlockExtent(
                                   pfsck_partition_info,
                                   partition_no,
                                   (1024 << pExt2SuperBlock->s_log_block_size) / SECTOR_SIZE,
                                   pExt2GroupDesc[blockGroupNo].bg_inode_bitmap,
                                   1,
                                   pfsck_partition_info->buffer);
    if(ret) {
        FSCK_LOG_ERROR("Could not read extent for inode bitmap",readPartitionBlockExtent,ret);
        return ret;
    }

    /* Check the bit in the bitmap for being used */
    return getBit(pfsck_partition_info->buffer,offset);
}


