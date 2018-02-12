#ifndef FSCK_LIST 
#define FSCK_LIST 1
#define MAX_ELEMENTS_PER_NODE 900 // cool number chosen to fit the page boundary for allocation


// !wow linked list :) 
struct element {
  __u32 idxNumber; // could be block or inode. 
  __u32 count; 
};

struct node {
  struct node *next; 
  int    next_free_element;
  struct element elements[MAX_ELEMENTS_PER_NODE];  
};

struct fsck_list_header {
  struct node *next;  
};

int fsck_list_init_header(struct fsck_list_header *pListHeader);
int fsck_list_add_element(struct fsck_list_header *pListHeader,struct element *pElt);
int fsck_free_list(struct fsck_list_header *pListHeader); 
int fsck_search_list(struct fsck_list_header *pListHeader,
		     struct element          *searchElt,
		     struct element         **ppElement);
int fsck_dump_list(struct fsck_list_header *pListHeader);

#endif // FSCK_LIST
