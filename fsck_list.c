/* 
   18-746: List management routines for FSCK for EXT2. 
   Faraz Shaikh 
   fshaikh@andrew.cmu.edu 
 */

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
#include <bio.h>
#include <fsck_list.h>
#include <stdlib.h>
#include <string.h>

int __fsck_list_allocate_node(struct node **ppNewNode) 
{
  *ppNewNode = (struct node *) malloc(sizeof(**ppNewNode));
  if(NULL == *ppNewNode) {
    FSCK_LOG_ERROR("Could not allocate memory for list", __fsck_list_allocate_node,errno);
    return ENOMEM; 
  } 
  memset(*ppNewNode,0,sizeof(**ppNewNode));
  return 0; 
}

int __fsck_list_free_node(struct node *pNode) {
  free(pNode);
  return 0; 
}

int fsck_list_init_header(struct fsck_list_header *pListHeader) {
  memset(pListHeader,0,sizeof(pListHeader));
  return 0;
}

int fsck_list_add_element(struct fsck_list_header *pListHeader,struct element *pElt){
  int ret; 
  struct node    *pNode=NULL;
  struct element *pexisting_element = NULL;    
 
  if(!pElt->idxNumber)
    printf("\nWarning i have been asked to insert a zero");


  /* if already existing increment the count */
  ret = fsck_search_list(pListHeader,
			 pElt,
			 &pexisting_element);
  if(0 == ret)
  {
      pexisting_element->count++;
      return ret;
  }
  ret = 0; // forget state.  

  /* allocate now its not present */
  if(NULL == pListHeader->next) {
    ret = __fsck_list_allocate_node(&pNode);
    if(ret)  return ret;
    else pListHeader->next = pNode; 
  }else if(MAX_ELEMENTS_PER_NODE <= pListHeader->next->next_free_element) {
    ret = __fsck_list_allocate_node(&pNode);
    if(ret)  return ret;
    else { pNode->next = pListHeader->next; pListHeader->next = pNode;}
  }else {
    pNode = pListHeader->next; 
  } 

  pElt->count = 1;  
  pNode->elements[pNode->next_free_element++] = *pElt;
  return 0; 
}


int fsck_free_list(struct fsck_list_header *pListHeader) {
  struct node *pNode; 
  while(pListHeader->next) {
    pNode = pListHeader->next; 
    pListHeader->next = pListHeader->next->next;
    __fsck_list_free_node(pNode);
  }
  memset(pListHeader,0,sizeof(*pListHeader));
  return 0; 
} 


int fsck_search_list(struct fsck_list_header *pListHeader,
		     struct element          *searchElt,
		     struct element         **ppElement) {
  int i; 
  struct node *pNode; 
  pNode = pListHeader->next; 
  while(pNode) {
    for(i=0;i<pNode->next_free_element;i++)
      if(searchElt->idxNumber == pNode->elements[i].idxNumber) {
	*ppElement = &(pNode->elements[i]);
	 return 0; 
      }
    pNode = pNode->next; 
  } 

  *ppElement = NULL; 
  return 1;
}


int fsck_dump_list(struct fsck_list_header *pListHeader) {
  int i; 
  struct node *pNode; 
  pNode = pListHeader->next;
  printf("\n"); 
  while(pNode) {
    for(i=0;i<pNode->next_free_element;i++)
      printf("\n [%d,%d]",pNode->elements[i].idxNumber,pNode->elements[i].count) ;
    pNode = pNode->next; 
  } 
  return 0;
}
