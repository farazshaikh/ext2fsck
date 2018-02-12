/*
   18-746: blockIO interface for EXT2 fsck.
   Faraz Shaikh
   fshaikh@andrew.cmu.edu
 */

#include <stdio.h>
#include <string.h>
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


int bio_clear_bio_block(pbio_t pbio) {
  memset(pbio->buffer,0,MAX_BIO_SIZE);
  return 0;
}

int bio_mark_dirty(pbio_t pbio) {
  pbio->dirty = 1;
  return 0;
}
int bio_clear_dirty(pbio_t pbio) {
  pbio->dirty = 0;
  return 0;
}

int bio_is_dirty(pbio_t pbio) {
  return(pbio->dirty);
}


int bio_fs_block_read(
		pfsck_partition_info_t pfsck_partition_info,
		int partition_no,
		int SectorsPerBlock,
		int StartBlock,
		pbio_t pbio){
  off_t  offset = 0;

  if(bio_is_dirty(pbio)) {
    FSCK_LOG_ERROR("Attempting to read into a dirty buffer.Consider flusing it first",bio_fs_block_read,0);
    return FSCK_ERR_IO;
  }

  offset = pfsck_partition_info->partition_table[partition_no].start_sect + (StartBlock * SectorsPerBlock);
  offset *= SECTOR_SIZE;

  pbio->disk_offset = offset;
  pbio->length      = SectorsPerBlock*SECTOR_SIZE;

  if(-1==lseek(pfsck_partition_info->fd,pbio->disk_offset,SEEK_SET)) {
     FSCK_LOG_ERROR("",read,errno);
     return(errno);
  }

  if(pbio->length !=
     read(pfsck_partition_info->fd,pbio->buffer,pbio->length)) {
     FSCK_LOG_ERROR("",read,errno);
     return(errno);
  }

  bio_clear_dirty(pbio);
  return 0;
}


int bio_fs_block_write(
		       pfsck_partition_info_t pfsck_partition_info,
                       pbio_t pbio){
  if(-1==lseek(pfsck_partition_info->fd,pbio->disk_offset,SEEK_SET)) {
     FSCK_LOG_ERROR("",read,errno);
     return(errno);
  }

  if(!bio_is_dirty(pbio))
         FSCK_LOG_ERROR("Block is clean, flushing it nevertheless",bio_fs_block_write,0);

  if(pbio->length  !=
     write(pfsck_partition_info->fd,pbio->buffer,pbio->length)) {
     FSCK_LOG_ERROR("",read,errno);
     return(errno);
  }

  bio_clear_dirty(pbio);
  return 0;
}
