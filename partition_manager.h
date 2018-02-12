#ifndef PARTION_MANAGER_H
#define PARTION_MANAGER_H

#include <genhd.h>
extern   int errno;
#define  SECTOR_SIZE      (0x1 << 9)
#define  MAX_PRIMARY_IDX  4
#define  MAX_PARTITIONS   256
#define  PARTITION_OFFSET 512 - 66
#define  MAX_BLOCK_SIZE   1024 << 3



#define FSCK_LOG_ERROR(Message,function,ret)\
do{\
  printf("\nFSCK ERROR: "#Message" %s returned %d",#function,errno);\
}while(0)

#define FSCK_LOG_ERROR_AND_EXIT(Message,function,ret)\
do{\
  FSCK_LOG_ERROR (Message,function,ret);\
  exit(ret);\
}while(0)

#define FSCK_DUMP_PARTION_INFO(ppartion_info)\
do {\
 printf("\n%0x,%s",(ppartion_info)->boot_ind,(ppartion_info)->boot_ind&0x8?"ACTIVE":"INACTIVE");\
 printf("\t%d-%d-%d",(ppartion_info)->cyl,(ppartion_info)->head,(ppartion_info)->sector);\
 printf("\t\t%d-%d-%d",(ppartion_info)->end_cyl,(ppartion_info)->end_head,(ppartion_info)->end_sector);\
 printf("\t0x%x%s",(ppartion_info)->sys_ind,\
 (ppartion_info)->sys_ind==0x83?"(EXT2)":\
 (ppartion_info)->sys_ind==0x82?"[SWAP]":\
 (ppartion_info)->sys_ind==0x5?"(EXTENDED)":"\tUNKNOWN\t");\
 printf("\t%d",(ppartion_info)->start_sect);\
 printf("\t%d",(ppartion_info)->nr_sects);\
}while(0)






typedef struct fsck_parition_info {
  int fd;
  char *buffer; 
  int   buffersize;

  /* Partition Tables */
  int               partitions_nr;
  int               current_partition;
  struct partition  partition_table[MAX_PARTITIONS];
}fsck_partition_info_t,*pfsck_partition_info_t;


int prepare_fsck(pfsck_partition_info_t pfsck_info,char *diskname);
int readDiskSector(pfsck_partition_info_t pfsck_info,long sectorNr);
int read_partiontable(pfsck_partition_info_t pfsck_info,char *diskname);
int readPartitionSectorExtent(
			      pfsck_partition_info_t pfsck_partition_info,
			      int partition_no,
			      int StartSector,
			      int Count,
			      char *buffer);

int readPartitionBlockExtent( 
			      pfsck_partition_info_t pfsck_partition_info,
			      int partition_no,
			      int SectorsPerBlock,
			      int StartBlock,
			      int Count,
			      char *buffer);





int writePartitionBlockExtent( 
			      pfsck_partition_info_t pfsck_partition_info,
			      int partition_no,
			      int SectorsPerBlock,
			      int StartBlock,
			      int Count,
			      char *buffer);

#endif // PARTION_MANAGER_H
