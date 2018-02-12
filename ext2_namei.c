#include <stdio.h>
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
#include <string.h>

int ext2_namei(
	       IN pfsck_partition_info_t    pfsck_partition_info,
	       IN int                       partition_no,
               IN struct ext2_super_block  *pExt2SuperBlock,
	       IN struct ext2_group_desc   *pExt2GroupDesc,
               IN char                     *path,
               OUT struct ext2_inode       *pExt2Inode,
	       OUT __u32                   *iNo) {
  struct ext2_inode root_inode; 
  int    ret; 
  char  *pathComponent;
  __u32 childinodeNumber = 0;

  if(strlen(path) <= 1 || '/' != path[0]) {
    FSCK_LOG_ERROR("Cannot traverse path",ext2_namei,FSCK_ERR_BAD_PATH);
    printf(" : %s",path);   
    return FSCK_ERR_BAD_PATH; 
  } 

  ret = read_ext2_get_inode(
		       pfsck_partition_info,
		       partition_no,
		       pExt2SuperBlock,
		       pExt2GroupDesc,
		       EXT2_ROOT_INO,
                       &root_inode);
  if(ret) {
    FSCK_LOG_ERROR("Could not read root inode", read_ext2_get_inode,ret);
    return ret;
  }

  if(strlen(path)==1 && '/' == path[0]) 
  {
    *pExt2Inode = root_inode; 
    return 0;  
  } 

  pathComponent = path; 
  while(1){
     
  
    pathComponent = strtok(pathComponent,"/");
    if(!pathComponent) 
      break; 
    
    /* Do a lookup */
    ret = ext2_lookup(
		      pfsck_partition_info,
		      partition_no, 
		      pExt2SuperBlock, 
                      pExt2GroupDesc, 
		      &root_inode,
		      pathComponent,
                      &childinodeNumber                       
                      );
    if(ret)
    {
      FSCK_LOG_ERROR("Cannot traverse path",ext2_namei,FSCK_ERR_PATH_DOES_NOT_EXIST);
      printf(" : %s",path);   
      return FSCK_ERR_PATH_DOES_NOT_EXIST; 
    }  

    /* Read the inode of the dirent */
    ret = read_ext2_get_inode(
		       pfsck_partition_info,
		       partition_no,
		       pExt2SuperBlock,
		       pExt2GroupDesc,
		       childinodeNumber,
                       &root_inode);
    if(ret) {
      FSCK_LOG_ERROR("Could not read child inode", read_ext2_get_inode,ret);
      return ret;
    }
    pathComponent = NULL;
  }

  /* So we now have the inode for the last component */ 
  /* If this is a directory LOG THE EVENT in real life we will return an error here*/
  if(root_inode.i_mode & EXT2_S_IFDIR)
    //   FSCK_LOG_ERROR("Last Component of the path is a directory "
    //  "Returning nevertheless", ext2_namei,0);  because  lost+found is a directory

  *iNo        = childinodeNumber;
  *pExt2Inode = root_inode;  
  return 0; 
}



int ext2_lookup(
	       IN pfsck_partition_info_t    pfsck_partition_info,
	       IN int                       partition_no,
               IN struct ext2_super_block  *pExt2SuperBlock,
	       IN struct ext2_group_desc   *pExt2GroupDesc,
               IN struct ext2_inode        *pExt2ParentInode,
               IN char                     *dirent_name, 
               OUT __u32                   *inodeNumber 
               ) 
{
int blockno=0; 
int ret;
int i;
struct ext2_dir_entry_2 *pDirent; 
bio_t     bio;  
bio_clear_dirty(&bio);

  //- Sanity checks -//   
  if(!(pExt2ParentInode->i_mode & EXT2_S_IFDIR)) {
    FSCK_LOG_ERROR("Inode doesn't point to an directory",read_ext2_dumpDirectoryContents,FSCK_ERR_INVALID_OPERATION);
    return FSCK_ERR_INVALID_OPERATION;
  }
 
  //- Search for the path component -//  
  while(1) {
    ret =  read_ext2_inode_data_bio(
			 pfsck_partition_info,
			 partition_no,
                         pExt2SuperBlock,
			 pExt2GroupDesc,
                         pExt2ParentInode,
			 blockno++,
			&bio); 
    if(ret==FSCK_ERR_LOGICAL_BLK_OFF_BOUNDS)
      return FSCK_ERR_PATH_DOES_NOT_EXIST;
    
    /* Search for the filename in this directory block */
    pDirent  =(struct ext2_dir_entry_2*)bio.buffer;
    //printf("\n");
    while(1)
    { 
      int minentrylen=0;
      
      /* we have searched this entire block Move to next block */
      if((char *)pDirent >= (char *)((char*)bio.buffer + bio.length))
	break;

      /* have we reached he last dirent for this directory */
      if(0 == pDirent->inode) 
        return FSCK_ERR_PATH_DOES_NOT_EXIST; 
     
      /* print the dirent out */
      //print_dirent(pDirent);

      /* Found !! */
      if(pDirent->name_len==strlen(dirent_name) && 
         !strncasecmp(pDirent->name,dirent_name,strlen(dirent_name)) )
      {
	   *inodeNumber = pDirent->inode;
	   return 0;   
      }
      
      /* next dirent */ 
      pDirent = (struct ext2_dir_entry_2*)((char *)pDirent + pDirent->rec_len);    
    } // end search for dirent in the block
  }
  return FSCK_ERR_PATH_DOES_NOT_EXIST;
}  
