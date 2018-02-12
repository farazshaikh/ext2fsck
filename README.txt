 ----------------------
|Compiling the code    |
 ----------------------
make -B
will generate ./myfsck


 ---------------------
|Running the program  |
 ---------------------
./myfsck ./diskname

   -Will read partition table
   -For each partition its finds of type ext2
    It will perform fsck.
