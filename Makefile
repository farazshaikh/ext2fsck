INCLUDE = .
SOURCES = fsck_list.c fsck_main.c partition_manager.c bio.c ext2_structures.c ext2_namei.c
myfsck	:	$(SOURCES)
	gcc -g $(SOURCES) -I $(INCLUDE) -o ext2_fsck_beta 
