/*
  18-746: partition manager for FSCK project2.
  Faraz Shaikh
  fshaikh@andrew.cmu.edu
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <genhd.h>
#include <partition_manager.h>
#include <ext2_structures.h>
#include <ext2_fs.h>
#include <ext2_namei.h>



/*******************************************************************************/
/* prepare_fsck                                                                */
/*      prepares s fsck_info structure to used for further use under           */
/*      fsck program                                                           */
/*                                                                             */
/* Input                                                                       */
/*     pfsck_info                                                              */
/*       Pointer to a fsck_info structure to be used a placeholder for         */
/*       init info.                                                            */
/*                                                                             */
/*     diskname                                                                */
/*       Zero terminated filename of the disk to be used for this fsck         */
/*       operation.                                                            */
/*******************************************************************************/
int prepare_disk(pfsck_partition_info_t pfsck_partition_info,char *diskname) {
    int temp = pfsck_partition_info->buffersize;
    memset(pfsck_partition_info,0,sizeof(*pfsck_partition_info));
    pfsck_partition_info->buffersize = temp;

    pfsck_partition_info->fd = open(diskname,O_RDWR);
    if(-1 == pfsck_partition_info->fd) {
        return(errno);
    }

    pfsck_partition_info->buffer = (char *)malloc(pfsck_partition_info->buffersize);
    if(NULL == pfsck_partition_info->buffer) {
        close(pfsck_partition_info->fd);
    }
    return 0;
}


/*****************************************************************************/
/* readSector                                                                */
/*     reads a specified sector number from the disk.                        */
/*                                                                           */
/* Input                                                                     */
/*       pfsck_partition_info                                                */
/*       pointer to an pre-initialized structure of type fsck_info.          */
/*                                                                           */
/* Returns                                                                   */
/*     0     on success                                                      */
/*     errno on error                                                        */
/*****************************************************************************/
int readDiskSector(pfsck_partition_info_t pfsck_partition_info,long sectorNr) {
    if(-1==lseek(pfsck_partition_info->fd,sectorNr * SECTOR_SIZE,SEEK_SET)) {
        FSCK_LOG_ERROR("",read,errno);
        return(errno);
    }

    if(pfsck_partition_info->buffersize !=
       read(pfsck_partition_info->fd,pfsck_partition_info->buffer,pfsck_partition_info->buffersize)) {
        FSCK_LOG_ERROR("",read,errno);
        return(errno);
    }
    return 0;
}


int readPartitionSectorExtent(
                              pfsck_partition_info_t pfsck_partition_info,
                              int partition_no,
                              int StartSector,
                              int Count,
                              char *buffer)
{
    off_t  offset = 0;

    offset = pfsck_partition_info->partition_table[partition_no].start_sect + StartSector;
    offset *= SECTOR_SIZE;

    if(-1==lseek(pfsck_partition_info->fd,offset,SEEK_SET)) {
        FSCK_LOG_ERROR("",read,errno);
        return(errno);
    }

    if(Count*SECTOR_SIZE !=
       read(pfsck_partition_info->fd,buffer,Count*SECTOR_SIZE)) {
        FSCK_LOG_ERROR("",read,errno);
        return(errno);
    }
    return 0;
}






int readPartitionBlockExtent(
                             pfsck_partition_info_t pfsck_partition_info,
                             int partition_no,
                             int SectorsPerBlock,
                             int StartBlock,
                             int Count,
                             char *buffer) {
    off_t  offset = 0;

    offset = pfsck_partition_info->partition_table[partition_no].start_sect + (StartBlock * SectorsPerBlock);
    offset *= SECTOR_SIZE;

    if(-1==lseek(pfsck_partition_info->fd,offset,SEEK_SET)) {
        FSCK_LOG_ERROR("",read,errno);
        return(errno);
    }

    if(Count*SectorsPerBlock*SECTOR_SIZE !=
       read(pfsck_partition_info->fd,buffer,Count*SectorsPerBlock*SECTOR_SIZE)) {
        FSCK_LOG_ERROR("",read,errno);
        return(errno);
    }
    return 0;
}


int writePartitionBlockExtent(
                              pfsck_partition_info_t pfsck_partition_info,
                              int partition_no,
                              int SectorsPerBlock,
                              int StartBlock,
                              int Count,
                              char *buffer)
{
    off_t  offset = 0;
    offset = pfsck_partition_info->partition_table[partition_no].start_sect + (StartBlock * SectorsPerBlock);
    offset *= SECTOR_SIZE;

    if(-1==lseek(pfsck_partition_info->fd,offset,SEEK_SET)) {
        FSCK_LOG_ERROR("",read,errno);
        return(errno);
    }

    if(Count*SectorsPerBlock*SECTOR_SIZE !=
       write(pfsck_partition_info->fd,buffer,Count*SectorsPerBlock*SECTOR_SIZE)) {
        FSCK_LOG_ERROR("",read,errno);
        return(errno);
    }
    return 0;
}


/***************************************************************************/
/* read_partiontable                                                       */
/*      read adn dumps the partition table to stdout                       */
/*                                                                         */
/* Input                                                                   */
/*      pfsck_partition_info                                               */
/*      pointer to an pre-initialized structure of type fsck_info.         */
/*                                                                         */
/* returns:                                                                */
/*     0     on  success                                                   */
/*     errno on error                                                      */
/***************************************************************************/
#define BOOT_MAGIC_X 0xaa55
int read_partiontable(pfsck_partition_info_t pfsck_partition_info,
                      char *diskname){
    int               extendedidx;
    char              buffer[SECTOR_SIZE];
    int               i=0;
    struct partition *ppartition;
    int               ebr_offset=0;
    int               ret;
    __u16              boot_magic;



    pfsck_partition_info->buffersize = MAX_BLOCK_SIZE;
    ret =  prepare_disk(pfsck_partition_info,diskname);
    if(ret)
        FSCK_LOG_ERROR_AND_EXIT("",prepare_fsck,ret);



    /* Read the MBR */
    ret = readDiskSector(pfsck_partition_info,0);
    if(ret) {
        FSCK_LOG_ERROR("",readSector,ret);
        return ret;
    }

    boot_magic = *((__u16 *)( pfsck_partition_info->buffer + SECTOR_SIZE - sizeof(__u16)));
    if(BOOT_MAGIC_X != boot_magic)
        {
            printf("\nThe boot MAGIC NUMBER is 0x%x bailing out",boot_magic);
            return(boot_magic);
        }

    printf("\nBootid\t\tStartCHS\tEndCHS\t\tType\t\tStart\tLength");
    /* dump out the information */
    ppartition = (struct partition *) ((char *)pfsck_partition_info->buffer + PARTITION_OFFSET);

    extendedidx = MAX_PRIMARY_IDX;
    for(i=0;i<4;i++){
        FSCK_DUMP_PARTION_INFO(ppartition);

        if(pfsck_partition_info->partitions_nr < MAX_PARTITIONS)
            pfsck_partition_info->partition_table[pfsck_partition_info->partitions_nr++] =
                *ppartition;

        if(DOS_EXTENDED_PARTITION == ppartition->sys_ind)
            extendedidx = i;

        ppartition = (struct partition*)
            ((char *)ppartition + sizeof(struct partition));
    }

    if(extendedidx == 5)
        return 0;


    ppartition = (struct partition *) ((char *)pfsck_partition_info->buffer + PARTITION_OFFSET);
    ppartition = (struct partition*)
        ((char *)ppartition + sizeof(struct partition) * extendedidx);

    ebr_offset = ppartition->start_sect;

    /* Read the EBR chain now */
    while(1) {
        /* Read the MBR */
        ret = readDiskSector(pfsck_partition_info,ebr_offset);
        if(ret) {
            FSCK_LOG_ERROR("",readSector,ret);
            return ret;
        }

        /* Dump the first logical disk */
        ppartition = (struct partition *) ((char *)pfsck_partition_info->buffer + PARTITION_OFFSET);
        ppartition->start_sect += ebr_offset;
        FSCK_DUMP_PARTION_INFO(ppartition);


        if(pfsck_partition_info->partitions_nr < MAX_PARTITIONS)
            pfsck_partition_info->partition_table[pfsck_partition_info->partitions_nr++] =
                *ppartition;


        /* Do we have another linked EBR */
        ppartition = (struct partition*)
            ((char *)ppartition + sizeof(struct partition));

        if(ppartition->nr_sects) {
            ebr_offset = ebr_offset + ppartition->start_sect;
        }
        else{
            break;
        }
    }

    /* All good now for return */
    return 0;
}


/* Unit testing code */
#define FSCK_MAX_PATH 1024
int assignment_partII(pfsck_partition_info_t pfsck_partition_info){
    int ret;
    int block_groups;
    char pathname[FSCK_MAX_PATH];
    __u32 childIno;

    struct ext2_super_block ext2_super_block;
    struct ext2_group_desc *pExt2GroupDesc=NULL;
    struct ext2_inode       ext2_inode;
    struct ext2_inode       ext2_inode_path_parse;
    bio_t  bio;
    bio_clear_dirty(&bio);


    /* read the super block of the partition 0 */
    ret = read_ext2_superblock(pfsck_partition_info,0,&ext2_super_block);
    if(ret) {
        FSCK_LOG_ERROR("Read superblock failed",read_ext2_superblock,ret);
        return ret;
    }

    if(EXT2_SUPER_MAGIC == ext2_super_block.s_magic)
        printf("\n\n\nPartition0:Superblock Magic=%x",ext2_super_block.s_magic);
    else {
        FSCK_LOG_ERROR("Superblock BAD MAGIC Number",read_ext2_superblock,EXT2_SUPER_MAGIC);
    }

    /* read the group descriptor for partition 0*/
    ret = read_ext2_block_descriptors(pfsck_partition_info,0,&ext2_super_block,&pExt2GroupDesc);
    if(ret) {
        FSCK_LOG_ERROR("Read group descriptors",read_ext2_block_descriptors,ret);
        return ret;
    }

    /* number of group descriptors */
    ret = read_ext2_get_inode(
                              pfsck_partition_info,
                              0,
                              &ext2_super_block,
                              pExt2GroupDesc,
                              EXT2_ROOT_INO,
                              &ext2_inode);
    if(ret) {
        FSCK_LOG_ERROR("Could not read root inode", read_ext2_get_inode,ret);
        return ret;
    }

    /* Sanity Check for the root Inode */
    if(ext2_inode.i_mode & EXT2_S_IFDIR) {
        printf("\n Root Inode is really an directory :)");
    }
    else {
        FSCK_LOG_ERROR("Root Inode is not a directory dymbfusk", read_ext2_get_inode,ret);
    }

    /*  Is the inode for the root inode really used */
    if(isInodeUsed(
                   pfsck_partition_info,
                   0,
                   &ext2_super_block,
                   pExt2GroupDesc,
                   EXT2_ROOT_INO
                   ))
        {
            printf("\n Root Inode bit is used in the inode Bitmap :)");
        }
    else {
        FSCK_LOG_ERROR("Root Inode bit is free in inode bitmap :(", read_ext2_get_inode,ret);
    }

    /* print out the contents of the root directory */
    printf("\nDirectory Listing for Root");
    printf("\nType\tInode\tName");
    ret = read_ext2_dumpDirectoryContents(
                                          pfsck_partition_info,
                                          0,
                                          &ext2_super_block,
                                          pExt2GroupDesc,
                                          &ext2_inode);
    if(ret){
        FSCK_LOG_ERROR("Contents of / cannot be read", read_ext2_dumpDirectoryContents,ret);
    }


    /* parse the path 1 */
    memset(pathname,0,FSCK_MAX_PATH);
    strncpy(pathname,"/lions/tigers/bears/ohmy.txt",FSCK_MAX_PATH);
    printf("\n Performing lookup for path %s",pathname);
    ret = ext2_namei(
                     pfsck_partition_info,
                     0,
                     &ext2_super_block,
                     pExt2GroupDesc,
                     pathname,
                     &ext2_inode_path_parse,
                     &childIno);
    strncpy(pathname,"/lions/tigers/bears/ohmy.txt",FSCK_MAX_PATH);
    if(ret) {
        FSCK_LOG_ERROR("Lookup Failed",ext2_namei,ret);
    }else {
        printf("\nNamei succeeded for path %s", pathname);
    }


    /* Parse path 3 */
    memset(pathname,0,FSCK_MAX_PATH);
    strncpy(pathname,"/oz/tornado/dorothy",FSCK_MAX_PATH);
    printf("\n\n\n Performing lookup for path %s",pathname);
    ret = ext2_namei(
                     pfsck_partition_info,
                     0,
                     &ext2_super_block,
                     pExt2GroupDesc,
                     pathname,
                     &ext2_inode_path_parse,
                     &childIno);
    strncpy(pathname,"/oz/tornado/dorothy",FSCK_MAX_PATH);
    if(ret) {
        FSCK_LOG_ERROR("Lookup Failed",ext2_namei,ret);
    }else {
        printf("\nNamei succeeded for path %s", pathname);
        printf("\n/oz/tornado/dorothy & /lions/tigers/bears/ohmy.txt are hardlinks to inode number 4021");
    }


    /* Parse Path 3 */
    memset(pathname,0,FSCK_MAX_PATH);
    strncpy(pathname,"/oz/tornado/glinda",FSCK_MAX_PATH);
    printf("\n\n\n Performing lookup for path %s",pathname);
    ret = ext2_namei(
                     pfsck_partition_info,
                     0,
                     &ext2_super_block,
                     pExt2GroupDesc,
                     pathname,
                     &ext2_inode_path_parse,
                     &childIno);
    strncpy(pathname,"/oz/tornado/glinda",FSCK_MAX_PATH);
    if(ret) {
        FSCK_LOG_ERROR("Lookup Failed",ext2_namei,ret);
    }else {
        printf("\nNamei succeeded for path %s", pathname);
    }

    /* check for symbolic link */
    if(ext2_inode_path_parse.i_mode & EXT2_S_IFLNK)
        {

            if(ext2_inode_path_parse.i_blocks) {

                printf("\nPath %s is a symbolic link", pathname);
                ret =  read_ext2_inode_data_bio(
                                                pfsck_partition_info,
                                                0,
                                                &ext2_super_block,
                                                pExt2GroupDesc,
                                                &ext2_inode_path_parse,
                                                0,
                                                &bio);
                if(ret==FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS)
                    printf("\nCould not read symbolic link target %s", pathname);
                else {
                    bio.buffer[ext2_inode_path_parse.i_size] = 0;
                    printf("\n%s->%s", pathname,bio.buffer);
                }
            }else {
                /* Fast symbolic link */
                (ext2_inode_path_parse.i_block[EXT2_N_BLOCKS]) = 0;
                printf("\n%s->%s", pathname,(char*)ext2_inode_path_parse.i_block);
            }
        }else{
        printf("\npath %s not a symbolic link",pathname);
    }

    return 0;
}


/*
  int main(int argc,char **argv) {
  int          ret;
  fsck_partition_info_t fsck_info;
  if(argc != 2) {
  print_usage();
  exit(-1);
  }

  fsck_info.buffersize = MAX_BLOCK_SIZE;

  // Read the partion table
  ret = read_partiontable(&fsck_info,argv[1]);
  if(ret)
  FSCK_LOG_ERROR_AND_EXIT("",prepare_fsck,ret);

  // Test part II of the assignment
  ret = assignment_partII(&fsck_info);
  if(ret)
  FSCK_LOG_ERROR_AND_EXIT("",prepare_fsck,ret);
  }*/

