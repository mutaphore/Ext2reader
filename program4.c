#include "ext2.h"

void traverse(char *arg, char opt) {
   ext2_inode *inode = rootInode;
   char *walker = arg, *c1, *name;
   
   //Scan through argument to get final inode
   while (*walker && *walker == '/') {
      c1 = walker++;
      for (; *walker && *walker != '/'; walker++)
         ;
      name = calloc(walker - c1 + 1, sizeof(char));
      memcpy(name, c1 + 1, walker - c1 - 1);
      inode = search(inode, name);
      free(name);
   }
   if (inode && opt != 'l')
      listDir(inode);
   else if (inode && opt == 'l')
      printContent(inode);
   else
      fprintf(stderr, "%s does not exist!\n", arg);
}

int main(int argc, char **argv) {

	if (argc < 2 || argc > 4)
      fprintf(stderr, "Usage: ext2reader <-l> <ext2-file> <directory or file>\n");
   else {
      if (!strcmp(*(argv + 1), "-l")) {
         if (!(fp = fopen(argv[2], "r"))) {
            fprintf(stderr, "Cannot open %s: %s\n", argv[2], strerror(errno));
            return -1;
         }
      }
      else {
         if (!(fp = fopen(argv[1], "r"))) {
            fprintf(stderr, "Cannot open %s: %s\n", argv[1], strerror(errno));
            return -1;
         }
      }
      //Initialize super block, block group descriptor table and root inode
      sb = calloc(1, sizeof(ext2_super_block));          
      read_data_1kb(1, 0, sb, sizeof(ext2_super_block));
      bgdTable = calloc(1, BLOCK_SIZE);
      read_data_1kb(2, 0, bgdTable, BLOCK_SIZE);
      rootInode = getInode(EXT2_ROOT_INO);
      if (argc == 2)
         listDir(rootInode);
      else if (argc == 3)
         traverse(*(argv + 2), 0);
      else if (argc == 4)
         traverse(*(argv + 3), 'l');
   }
	return 0;
}
