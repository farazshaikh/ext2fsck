#ifndef BIO_H
#define BIO_H

#define MAX_SECTOR_PER_BIO 10
#define MAX_BIO_SIZE       SECTOR_SIZE * MAX_SECTOR_PER_BIO
 
typedef struct _bio_t {
    int    dirty; 
    off_t  disk_offset;
    int    length; 
    char   buffer[MAX_BIO_SIZE];
}bio_t,*pbio_t; 


int bio_fs_block_read(
		pfsck_partition_info_t pfsck_partition_info,
		int    partition_no,
		int    SectorsPerBlock,
		int    FsBlockNo,
		pbio_t pbio);

int bio_fs_block_write(
  	       pfsck_partition_info_t pfsck_partition_info,
                pbio_t pbio);
int bio_clear_bio_block(pbio_t);
int bio_mark_dirty(pbio_t);
int bio_clear_dirty(pbio_t);
int bio_is_dirty(pbio_t);  
#endif 
