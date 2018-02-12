#ifndef EXT2_NAMEI  
#define EXT2_NAMEI 1 
#include <ext2_fs.h>
#include <fsck_errors.h>
#include <partition_manager.h>

int ext2_namei(
	       IN pfsck_partition_info_t    pfsck_partition_info,
	       IN int                       partition_no,
               IN struct ext2_super_block  *pExt2SuperBlock,
	       IN struct ext2_group_desc   *pExt2GroupDesc,
               IN char                     *path,
               OUT struct ext2_inode       *pExt2Inode,
	       OUT __u32 *);


int ext2_lookup(
	       IN pfsck_partition_info_t    pfsck_partition_info,
	       IN int                       partition_no,
               IN struct ext2_super_block  *pExt2SuperBlock,
	       IN struct ext2_group_desc   *pExt2GroupDesc,
               IN struct ext2_inode        *pExt2ParentInode,
               IN char                     *dirent_name 
               );  
#endif //EXT2_NAMEI
