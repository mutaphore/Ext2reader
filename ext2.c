#include "ext2.h"

FILE *fp = NULL;								//Pointer to ext2 file
ext2_super_block *sb = NULL;				//Super block
ext2_group_desc *bgdTable = NULL;		//Block group descriptor table
ext2_inode *rootInode = NULL;				//Root inode

//the block argument is in terms of SD card 512 byte sectors
void read_data(uint32_t block, uint16_t offset, uint8_t* data, uint16_t size) {
   if (offset > 511) {
      printf ("Offset greater than 511.\n");
      exit(0);
   }

   fseek(fp,block*512 + offset,SEEK_SET);
   fread(data,size,1,fp);
}

//Block reader in 1KB blocks, wrapper on the 512b version
void read_data_1kb(uint32_t block, uint16_t offset, uint8_t *data, uint16_t size) {
	
	block *= 2;
	while (offset > 511) {
		block++;
		offset -= 512;
	}
	read_data(block, offset, data, size);
}

//Given an inode number, find and return the inode address in memory
ext2_inode *getInode(uint32_t iNum) {
	uint32_t blockGroupNum = (iNum - 1) / sb->s_inodes_per_group;
	uint32_t locNdx = (iNum - 1) % sb->s_inodes_per_group;
	ext2_inode *inode, *inodeTable;

	inode = calloc(1, sizeof(ext2_inode));
	read_data_1kb(bgdTable[blockGroupNum].bg_inode_table, 
	 locNdx * sizeof(ext2_inode), inode, sizeof(ext2_inode));

	return inode;
}

//Check block bitmap to see if a block is used or not (debug only)
uint32_t checkUsed(uint32_t blk) {
	Block *bitmap = calloc(1, sizeof(Block));
	uint32_t bgNum = blk / sb->s_blocks_per_group; //start at block 0
	uint32_t locNdx, intNdx, bitNdx, used;

	locNdx = blk % sb->s_blocks_per_group;
	intNdx = locNdx / 256;
	bitNdx = locNdx % 256;

	printf("bits: %d %d %d %d\n", blk, locNdx, intNdx, bitNdx);

	read_data_1kb(bgdTable[bgNum].bg_block_bitmap, 0, bitmap, sizeof(Block));
	used = ((bitmap->items)[intNdx] & 1 << bitNdx) >> bitNdx;
	free(bitmap);

	return used;
}

//Adds an entry node to a sorted linked list 
static void addEntry(Node **head, ext2_dir_entry *entry) {
	Node *prev = NULL, *temp = *head, *nd;

	nd = calloc(1, sizeof(Node));
	nd->entry = entry;
	while (temp && strncmp(temp->entry->name, entry->name, entry->name_len) < 0) {
		prev = temp;
		temp = temp->next;
	}
	if (prev) {
		nd->next = prev->next;
		prev->next = nd;
	}
	else {
		nd->next = *head;
		*head = nd;
	}
}

//Print linked list elements
static void printEntries(Node *head) {
	ext2_inode *temp;

	printf("----------------------------------------\n");
	printf("%-20s%-15s%-5s\n", "Name", "Size", "Type");
	printf("----------------------------------------\n");
	while (head) {
		temp = getInode(head->entry->inode);
		printf("%-20.*s", head->entry->name_len, head->entry->name);
		if (temp->i_mode >> 12 == EXT2_DIR)
			printf("%-15d%-5s\n", 0, "D");
		else if (temp->i_mode >> 12 == EXT2_FILE)
			printf("%-15d%-5s\n", temp->i_size, "F");
		head = head->next;
	}
}

//Free all dynamically allocated memory in linked list
static void freeEntries(Node *head) {
	Node *temp = head;

	for (; head; temp = head) {
		free(temp->entry);
		free(temp);
		head = head->next;
	}
}

//Prints entries under an inode
void listDir(ext2_inode *inode) {
	ext2_dir_entry *entry, temp;
	Node *entryList = NULL;
	uint32_t i = 0, offset = 0;

	read_data_1kb(inode->i_block[i], offset, &temp, sizeof(ext2_dir_entry));

	do {
		entry = calloc(1, temp.name_len + 8);
		read_data_1kb(inode->i_block[i], offset, entry, temp.name_len + 8);
		addEntry(&entryList, entry);

		offset += temp.rec_len;
		if (offset >= BLOCK_SIZE) {
			offset = 0;
			i++;
		}
		read_data_1kb(inode->i_block[i], offset, &temp, sizeof(ext2_dir_entry));
	} while (i < inode->i_blocks && temp.inode);

	printEntries(entryList);
	freeEntries(entryList);
}

//Searches for an entry under inode and return its inode if found
ext2_inode *search(ext2_inode *inode, char *name) {
	uint32_t i = 0, offset = 0, len;
	ext2_dir_entry temp, *entry;

	read_data_1kb(inode->i_block[i], offset, &temp, sizeof(ext2_dir_entry));

	do {
		entry = calloc(1, temp.name_len + 8);
		read_data_1kb(inode->i_block[i], offset, entry, temp.name_len + 8);
		len = strlen(name) > entry->name_len ? strlen(name) : entry->name_len;
		if (!strncmp(entry->name, name, len)) {
			free(entry);
			return getInode(entry->inode);
		}
		free(entry);

		offset += temp.rec_len;
		if (offset >= BLOCK_SIZE) {
			offset = 0;
			i++;
		}
		read_data_1kb(inode->i_block[i], offset, &temp, sizeof(ext2_dir_entry));
	} while (i < inode->i_blocks && temp.inode);

	return NULL;
}

//Prints the contents of a file type inode
void printContent(ext2_inode *inode) {
	uint32_t i, size, n1, n2, n3;
	Block dBlock, indBlock, dIndBlock;
	char *chr;

	size = inode->i_size;

	for (i = 0; size && i < EXT2_N_BLOCKS; i++) {
		read_data_1kb(inode->i_block[i], 0, &dBlock, BLOCK_SIZE);
		if (i < EXT2_NDIR_BLOCKS) {
			for (n1 = 0, chr = &dBlock; size && n1 < BLOCK_SIZE; n1++, size--)
				printf("%c", chr[n1]);
		}
		else if (i == EXT2_IND_BLOCK) {
			for (n1 = 0; size && n1 < BLOCK_ITEMS; n1++) {
				read_data_1kb(dBlock.items[n1], 0, &indBlock, BLOCK_SIZE);
				for (n2 = 0, chr = &indBlock; size && n2 < BLOCK_SIZE; n2++, size--)
					printf("%c", chr[n2]);
			}
		}
		else if (i == EXT2_DIND_BLOCK) {
			for (n1 = 0; size && n1 < BLOCK_ITEMS; n1++) {
				read_data_1kb(dBlock.items[n1], 0, &indBlock, BLOCK_SIZE);
				for (n2 = 0; size && n2 < BLOCK_ITEMS; n2++) {
					read_data_1kb(indBlock.items[n2], 0, &dIndBlock, BLOCK_SIZE);
					for (n3 = 0, chr = &dIndBlock; size && n3 < BLOCK_SIZE; n3++, size--)
						printf("%c", chr[n3]);
				}
			}
		}
	}
}
