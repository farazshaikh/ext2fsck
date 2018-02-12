#!/bin/sh
echo "COMPILING"
make -B


echo "FREEING LOOPBACKS FOR TESTING"
losetup -d /dev/loop1
losetup -d /dev/loop2
losetup -d /dev/loop3


echo "EXTRACTING DISK"
cd .. 
tar -zxvf ./18746_proj2_files.tar.gz
cd -


echo "RUNNING 18-746 FSCK"
./myfsck ../disk


echo "NOW RUNNING THE REAL EXT2 FILESYSTEM CHECK UTILITY FOR CROSS VERIFICATION"
losetup -o 32256    /dev/loop1 ../disk
losetup -o 24675840 /dev/loop2 ../disk
losetup -o 57609216 /dev/loop3 ../disk 

/sbin/fsck.ext2 /dev/loop1
/sbin/fsck.ext2 /dev/loop2
/sbin/fsck.ext2 /dev/loop3

losetup -d /dev/loop1
losetup -d /dev/loop2
losetup -d /dev/loop3
echo "END"
