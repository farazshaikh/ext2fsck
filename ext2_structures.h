#ifndef EXT2_STRUCTURES
#define EXT2_STRUCTURES
#include <ext2_fs.h>
#include <fsck_errors.h>
#include <fsck_list.h>
#include <bio.h>


#define EXT2_FT_UNKNOWN 0 
#define EXT2_FT_REG_FILE 1 
#define EXT2_FT_DIR 2 
#define EXT2_FT_CHRDEV 3 
#define EXT2_FT_BLKDEV 4 
#define EXT2_FT_FIFO 5 
#define EXT2_FT_SOCK 6 
#define EXT2_FT_SYMLINK 7 
#define EXT2_FT_MAX 8

#define EXT2_S_IFMT   0xF000 //format mask
#define EXT2_S_IFSOCK 0xC000 //socket
#define EXT2_S_IFLNK  0xA000//symbolic link
#define EXT2_S_IFREG  0x8000//regular file
#define EXT2_S_IFBLK  0x6000//block device
#define EXT2_S_IFDIR  0x4000//directory
#define EXT2_S_IFCHR  0x2000//character device
#define EXT2_S_IFIFO  0x1000//fifo

#define LINUX_EXT2_PARTITION  0x83

__u8    imodetodirft(__u16   i_mode);

int read_ext2_superblock(pfsck_partition_info_t,int partition_no,struct ext2_super_block *);

int read_ext2_block_descriptors( 
			 IN  pfsck_partition_info_t   pfsck_partition_info,
			 IN  int                      partition_no,
                         IN  struct ext2_super_block *pExt2SuperBlock,
			 OUT struct ext2_group_desc **ppExt2GroupDesc);

int read_ext2_get_inode(
		    IN pfsck_partition_info_t    pfsck_partition_info,
		    IN int                       partition_no,
		    IN struct ext2_super_block  *pExt2SuperBlock,
		    IN struct ext2_group_desc   *pExt2GroupDesc,
		    IN unsigned long             inodenumber,
                    OUT struct ext2_inode       *pExt2Inode) ;

int read_ext2_put_inode(
		    IN pfsck_partition_info_t    pfsck_partition_info,
		    IN int                       partition_no,
		    IN struct ext2_super_block  *pExt2SuperBlock,
		    IN struct ext2_group_desc   *pExt2GroupDesc,
		    IN unsigned long             inodenumber,
                    IN struct ext2_inode        *pExt2Inode) ;


int read_ext2_LCN_TO_VCN(
			 IN  pfsck_partition_info_t   pfsck_partition_info,
			 IN  int                      partition_no,
                         IN  struct ext2_super_block *pExt2SuperBlock,
			 IN  struct ext2_group_desc  *pExt2GroupDesc,
                         IN  struct ext2_inode       *pExt2Inode,
			 IN  long                     logicalBlockNo,
                         IN  struct fsck_list_header *pnsDataListHeader,
                         OUT __u32                   *pVCN);


int read_ext2_inode_data_bio(
			 IN  pfsck_partition_info_t   pfsck_partition_info,
			 IN  int                      partition_no,
                         IN  struct ext2_super_block *pExt2SuperBlock,
			 IN  struct ext2_group_desc  *pExt2GroupDesc,
                         IN  struct ext2_inode       *pExt2Inode,
			 IN  long                     logicalBlockNo,
                         OUT pbio_t                   pbio);

int read_ext2_dumpDirectoryContents( 
			 IN  pfsck_partition_info_t   pfsck_partition_info,
			 IN  int                      partition_no,
                         IN  struct ext2_super_block *pExt2SuperBlock,
			 IN  struct ext2_group_desc  *pExt2GroupDesc,
                         IN  struct ext2_inode       *pExt2DirInode);


int isBlockUsed(
		IN pfsck_partition_info_t    pfsck_partition_info,
		IN int                       partition_no,
		IN struct ext2_super_block  *pExt2SuperBlock,
		IN struct ext2_group_desc   *pExt2GroupDesc,
		IN unsigned long             blockNo
                ); 

int isInodeUsed(
		IN pfsck_partition_info_t    pfsck_partition_info,
		IN int                       partition_no,
		IN struct ext2_super_block  *pExt2SuperBlock,
		IN struct ext2_group_desc   *pExt2GroupDesc,
		IN unsigned long             InodeNo
                ); 

int print_dirent(struct ext2_dir_entry_2 *pDirent);
 
#endif// EXT2_STRUCTURES
