/* readwrite.c
 * 
 * Code to read and write sectors to a "disk" file.
 * This is a support file for the "fsck" storage systems laboratory.
 * 
 * author: YOUR NAME HERE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>	/* for memcpy() */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(__FreeBSD__)
#define lseek64 lseek
#endif

/* linux: lseek64 declaration needed here to eliminate compiler warning. */
extern int64_t lseek64(int, int64_t, int);


const unsigned int sector_size__bytes=512;

int device;  /* disk file descriptor */


/* print_sector: print the contents of a buffer containing one sector.
 *
 * inputs:
 *   char *buf: buffer must be >= 512 bytes.
 *
 * outputs:
 *   the first 512 bytes of char *buf are printed to stdout.
 *
 * modifies:
 *   (none)
 */
void
print_sector (unsigned char *buf)
{
  int i;
  for (i=0; i<sector_size__bytes; i++) {
    printf("%02x", buf[i]);
    if (!((i+1)%32)) printf("\n");	/* line break after 32 bytes */
    else if (!((i+1)%4)) printf(" ");	/* space after 4 bytes */
  }
}


/* read_sectors: read a specified number of sectors into a buffer.
 *
 * inputs:
 *   int64 block: the starting sector number to read.
 *                sector numbering starts with 0.
 *   int numsectors: the number of sectors to read.  must be >= 1.
 *   int device [GLOBAL]: the disk from which to read.
 *
 * outputs:
 *   void *into: the requested number of blocks are copied into here.
 *
 * modifies:
 *   void *into
 */
void
read_sectors (int64_t block, unsigned int numsectors, void *into)
{
  int ret;
  int64_t lret;

  if (1) {
    if (numsectors == 1) {
      printf("Reading sector  %lld\n", block);
    } else {
      printf("Reading sectors %lld--%lld\n", block, block + (numsectors-1));
    }
  }

  if ((lret = lseek64(device, block * sector_size__bytes, SEEK_SET)) 
      != block * sector_size__bytes) {
    fprintf(stderr, "Seek to position %lld failed: returned %lld\n", 
	    block * sector_size__bytes, lret);
    exit(-1);
  }
  if ((ret = read(device, into, sector_size__bytes * numsectors)) 
      != sector_size__bytes * numsectors) {
    fprintf(stderr, "Read block %lld length %d failed: returned %d\n", 
	    block, numsectors, ret);
    exit(-1);
  }
}


/* write_sectors: write a buffer into a specified number of sectors.
 *
 * inputs:
 *   int64 block: the starting sector number to write.
 *                sector numbering starts with 0.
 *   int numsectors: the number of sectors to write.  must be >= 1.
 *   void *from: the requested number of blocks are copied from here.
 *
 * outputs:
 *   int device [GLOBAL]: the disk into which to write.
 *
 * modifies:
 *   int device [GLOBAL]
 */
void
write_sectors (int64_t block, unsigned int numsectors, void *from)
{
  int ret;
  int64_t lret;

  if (1) {
    if (numsectors == 1) {
      printf("Reading sector  %lld\n", block);
    } else {
      printf("Reading sectors %lld--%lld\n", block, block + (numsectors-1));
    }
  }

  if ((lret = lseek64(device, block * sector_size__bytes, SEEK_SET)) 
      != block * sector_size__bytes) {
    fprintf(stderr, "Seek to position %lld failed: returned %lld\n", 
	    block * sector_size__bytes, lret);
    exit(-1);
  }
  if ((ret = write(device, from, sector_size__bytes * numsectors)) 
      != sector_size__bytes * numsectors) {
    fprintf(stderr, "Read block %lld length %d failed: returned %d\n", 
	    block, numsectors, ret);
    exit(-1);
  }
}


int
main (int argc, char **argv)
{
  /* This is a sample program.  If you want to print out sector 57 of
   * the disk, then run the program as:
   *
   *    ./readwrite disk 57
   *
   * You'll of course want to replace this with your own functions.
   */

  unsigned char buf[sector_size__bytes];	/* temporary buffer */
  int           the_sector;			/* IN: sector to read */

  if ((device = open(argv[1], O_RDWR)) == -1) {
    perror("Could not open device file");
    exit(-1);
  }

  the_sector = atoi(argv[2]);
  printf("Dumping sector %d:\n", the_sector);
  read_sectors(the_sector, 1, buf);
  print_sector(buf);

  close(device);
  return 0;
}

/* EOF */
