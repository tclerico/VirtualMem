/*
Main program for the virtual memory project.
Make all of your modifications to this file.
You may add or rearrange any code or data as you need.
The header files page_table.h and disk.h explain
how to use the page table and disk interfaces.
*/

#include "page_table.h"
#include "disk.h"
#include "program.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int find_free_frame();
void remove_page(struct page_table *pt, int fnumber);

void rand_replace(struct page_table *pt, int page);
void fifo(struct page_table *pt, int page);
void lru(struct page_table *pt, int page);


//must be global because used in page fault handler
char* virtmem = NULL;
char* physmem = NULL;
struct disk* disk = NULL;

int framecount = 0;
char *alg;

int  nframes;
int npages;

typedef struct
{
	int page;
	int bits;
} frameEntry;

frameEntry* frameTable = NULL;

//used to track statistics to print at the end
int pageFaults = 0;
int diskReads = 0;
int diskWrites = 0;

//globals for FIFO
int front = 0;
int end = 0;
int *fifoArray;

//LRU Stuff
struct node{
	int page;
	struct node *next;
	struct node *previous;
};

//LRU functions
void push(int page);
int find_in_stack(int page);
void move_to_top(struct node *toTop);
int drop(struct page_table *pt);
struct node* GetNewNode(int page);

struct node *top = NULL;
struct node *bottom = NULL;


//returns the first free frame, once frame table is full only returns -1
int find_free_frame(){
	int i;
	for(i=0; i<nframes; i++){
		if(frameTable[i].bits == 0){
			return i;
		}
	}
	return -1;

}

void remove_page(struct page_table *pt, int fnumber){
	//if 'dirty bit' write to disk
	if(frameTable[fnumber].bits & PROT_WRITE){
		disk_write(disk,frameTable[fnumber].page, &physmem[fnumber*PAGE_SIZE]);
		diskWrites++;
	}
	//update page + frame table
	page_table_set_entry(pt, frameTable[fnumber].page, fnumber, 0);
	frameTable[fnumber].bits = 0;
}

//creates a new node and returns a pointer to it
struct node* GetNewNode(int page){
	struct node* newNode = (struct node*)malloc(sizeof(struct node));
	newNode->page = page;
	newNode->next = NULL;
	newNode->previous = NULL;
	return newNode;
}

//pushes a new node to the top of the stack
void push(int page){
	struct node* newNode = GetNewNode(page);
	if(top == NULL && bottom == NULL){
		top = newNode;
		bottom = newNode;
		return;
	}
	top->next = newNode;
	newNode->previous = top;
	top = newNode;
}

//search for a given page in the table
int find_in_stack(int page){
	struct node *temp = top;
	int index = 0;
	while(temp!=NULL){
		if(temp->page == page){
			return index;
		}
		temp = temp->previous;
		index++;
	}
	return -1;

}

//moves an existing node to the top of the stack (for when an existing page is referenced)
void move_to_top(struct node *toTop){
	//if already at top
	if(top == toTop){
		return;
	}
		//if at bottom of stack
	else if(toTop->previous == NULL){
		bottom = toTop->next;
		top->next = toTop;
		top = toTop->previous;
		toTop->next = NULL;
		top = toTop;
	}
	else{
		//create temp to point to the previous node
		struct node *temp = toTop->previous;

		//re-route pointers of surrounding nodes
		temp->next = toTop->next;
		temp->next->previous = temp;

		//move target node to top
		toTop->next = NULL;
		toTop->previous = top;
		top->next = toTop;
		top = toTop;

	}
}

//prints out all pages currently in stack
void print_stack(){
	struct node *temp = top;

	while(temp!=NULL){
		printf("%d, ",temp->page);
		temp=temp->previous;
	}

}


//removes the last node from the stack and removes the page from the frame table
int drop(struct page_table *pt){
	int frame,bits;
	page_table_get_entry(pt,bottom->page,&frame,&bits);
	//printf("Releasing Frame: %d\n",frame);

	//remove bottom node and page from table
	remove_page(pt, frame);

	//printf("Removing Node From Stack\n");
	//remove node from list
	struct node *temp = bottom;
	temp->next->previous = NULL;
	bottom = bottom->next;
	free(temp);

	//printf("returning from drop\n");
	return frame;
}


void rand_replace(struct page_table *pt, int page) {

	//printf("In random replacement\n");
	//int nframes = page_table_get_nframes(pt);
	//srand(NULL);
	//printf("Seeded random\n");

	//
	int frame;
	int bits;
	page_table_get_entry(pt, page, &frame, &bits);
	//printf("frame: %d\n", frame);
	//printf("bits: %d\n", bits);


	int index;
	if (!bits) {
		//set to prot_read
		bits = PROT_READ;
		//find a free frame -> or get -1
		index = find_free_frame();
		//if there were no free frames
		if (index < 0) {
            //get random frame to replace
            index = (int) lrand48() % nframes;
            //remove page from frame
            remove_page(pt, index);
        }
		//read from disk into frame
		disk_read(disk, index, &physmem[index*PAGE_SIZE]);
		diskReads++;
	}
	else if(bits & PROT_READ){
		//give write privledge and set index to the returned frame
		bits = PROT_WRITE | PROT_READ;
		index = frame;
	}
	else{
		printf("ERROR ON PAGE FAULT");
		exit(1);
	}
	//update page table and frame table
	page_table_set_entry(pt, page, index, bits);
	frameTable[index].page = page;
	frameTable[index].bits = bits;

}

void fifo(struct page_table *pt, int page){
	//get current frame + bit
	int frame, bit;
	page_table_get_entry(pt,page,&frame,&bit);

	//use to keep track of frame
	int index;
	//if bit not set -> read into page table
	if(!bit){
		//set bit to protected read
		bit = PROT_READ;

		//check for free frame
		index=find_free_frame();
		//printf("index: %d\n",index);
//		printf("front: %d\n",front);
//		printf("end: %d\n",end);

		if(index < 0){

			if(front==end){
				//printf("attempting to remove first entry\n");
				//select frame from front of array and remove
				index = fifoArray[front];
				//printf("updated index: %d\n",index);
				remove_page(pt,index);
			}
			else{
				printf("Error in FIFO\n");
				exit(1);
			}
		}
		//read from the disk
		disk_read(disk, page, &physmem[index*PAGE_SIZE]);
		diskReads++;
		//update the end pointer of array
		fifoArray[end] = index;
		end = (end) % nframes;

	}
	else if(bit & PROT_READ){
		bit = PROT_READ | PROT_WRITE;
		index = frame;
	}
	else{
		printf("ERROR in FIFO");
		exit(1);
	}

	//update tables
	page_table_set_entry(pt, page, index, bit);
	frameTable[index].page = page;
	frameTable[index].bits = bit;
}


void lru(struct page_table *pt, int page){
	//get current frame + bit
	int frame, bits;
    int exists = -1;
	page_table_get_entry(pt,page,&frame,&bits);

	//use to keep track of frame
	int index;
	//printf("Bits: %d\n",bits);
	if(!bits){
		//try to find free frame
		index = find_free_frame();

		bits = PROT_READ;

		//if there are no free frames
		if(index < 0){
			//search for page in stack
            //printf("Searching stack for page #: %d\n",page);
			exists = find_in_stack(page);
			//printf("Page found at: %d\n",exists);
			//printf("After search\n");
			//if exists in stack
			if(exists>=0){
                //printf("Frame Exists\n");
				struct node *temp = top;
				for(int i=0; i<exists; i++){
					temp = temp->previous;
				}
				move_to_top(temp);
			}
			else if(exists<0){
				//remove the bottom node from the stack and page from table
				//printf("Attempting drop\n");
				index = drop(pt);
				//printf("Frame Index: %d\n",index);
			}
		}
		//read from disk
        //printf("reading from disk\n");
		disk_read(disk, page, &physmem[index*PAGE_SIZE]);
		diskReads++;
		//push new page to the stack
        if(exists<0){
            //printf("Pushing\n");
            push(page);
        }

	}
	//if bits are prot_write set it so that it is dirty
	else if(bits & PROT_READ){
		//printf("Setting dirty bit\n");
		bits = PROT_READ | PROT_WRITE;
		//printf("Nbits: %d\n",bits);
		index = frame;
	}
	else{
		printf("ERROR in LRU\n");
		exit(1);
	}
	//update page table
    //bits = PROT_READ;
	page_table_set_entry(pt,page,index,bits);

	///update frame table
	frameTable[index].page = page;
	frameTable[index].bits = bits;

	//print out stack
	//print_stack();

}




void page_fault_handler( struct page_table *pt, int page)
{
	pageFaults++;

	//if full -> call function for replacement algorithm
	if(!strcmp(alg,"rand")){
		//printf("Calling Random Replacement\n");
		rand_replace(pt,page);
	}
	else if(!strcmp(alg,"fifo")){
		fifo(pt,page);
	}
	else{
		lru(pt,page);
	}

	//printf("page fault on page #%d\n",page);
	//exit(1);
}





int main( int argc, char *argv[] )
{
	if(argc!=5) {
		printf("use: virtmem <npages> <nframes> <rand|fifo|lru> <sort|scan|focus>\n");
		return 1;
	}

	npages = atoi(argv[1]);
	nframes = atoi(argv[2]);
	alg = argv[3];
	const char *program = argv[4];

	disk = disk_open("myvirtualdisk",npages);
	if(!disk) {
		fprintf(stderr,"couldn't create virtual disk: %s\n",strerror(errno));
		return 1;
	}


	struct page_table *pt = page_table_create( npages, nframes, page_fault_handler );
	if(!pt) {
		fprintf(stderr,"couldn't create page table: %s\n",strerror(errno));
		return 1;
	}

	virtmem = page_table_get_virtmem(pt);

	physmem = page_table_get_physmem(pt);

	//allocate memory for the FIFO array
	fifoArray = malloc(nframes * sizeof(int));


	//allocate mem for Frame Table
	frameTable = malloc(nframes * sizeof(frameEntry));
	if (frameTable == NULL)
	{
		printf("Error allocating space for frame table!\n");
		exit(1);
	}


	if(!strcmp(program,"sort")) {
		sort_program(virtmem,npages*PAGE_SIZE);

	} else if(!strcmp(program,"scan")) {
		scan_program(virtmem,npages*PAGE_SIZE);

	} else if(!strcmp(program,"focus")) {
		focus_program(virtmem,npages*PAGE_SIZE);

	} else {
		fprintf(stderr,"unknown program: %s\n",argv[4]);

	}

	printf("Page Faults: %d, ",pageFaults);
	printf("Disk Reads: %d, ",diskReads);
	printf("Disk Writes: %d\n",diskWrites);

	free(fifoArray);
	free(frameTable);
	page_table_delete(pt);
	disk_close(disk);

	return 0;
}

