#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fs.h"
#include "fs_sim.h"
#include "fs_util.h"
#include "disk.h"

char inodeMap[MAX_INODE / 8];
char blockMap[MAX_BLOCK / 8];
Inode inode[MAX_INODE];
SuperBlock superBlock;
Dentry curDir;
int curDirBlock;

int fs_mount(char *name)
{
	int numInodeBlock =  (sizeof(Inode)*MAX_INODE)/ BLOCK_SIZE;
	int i, index, inode_index = 0;

	// load superblock, inodeMap, datakMap and inodes into the memory

	if(disk_mount(name) == 1) {
		disk_read(0, (char*) &superBlock); 				// Read from disk and write to superblock
		if(superBlock.magicNumber != MAGIC_NUMBER) {
				printf("Invalid disk!\n");
				exit(0);
		}
		disk_read(1, inodeMap);							// Read from disk and write to inodeMap
		disk_read(2, blockMap); 						// Read from disk and write to blockMap
		for(i = 0; i < numInodeBlock; i++)
		{
				index = i+3;
				disk_read(index, (char*) (inode+inode_index));
				inode_index += (BLOCK_SIZE / sizeof(Inode));
		}
		// root directory
		curDirBlock = inode[0].directBlock[0];
		disk_read(curDirBlock, (char*)&curDir);

	} else {
		// Init file system superblock, inodeMap and blockMap
		superBlock.magicNumber = MAGIC_NUMBER;
		superBlock.freeBlockCount = MAX_BLOCK - (1+1+1+numInodeBlock);
		superBlock.freeInodeCount = MAX_INODE;

		//Init inodeMap
		for(i = 0; i < MAX_INODE / 8; i++)
		{
			set_bit(inodeMap, i, 0);
		}
		//Init blockMap
		for(i = 0; i < MAX_BLOCK / 8; i++)
		{
			if(i < (1+1+1+numInodeBlock)) set_bit(blockMap, i, 1);
			else set_bit(blockMap, i, 0);
		}
		//Init root dir
		int rootInode = get_free_inode();
		curDirBlock = get_free_block();

		inode[rootInode].type = directory;
		inode[rootInode].owner = 0;
		inode[rootInode].group = 0;
		gettimeofday(&(inode[rootInode].created), NULL);
		gettimeofday(&(inode[rootInode].lastAccess), NULL);
		inode[rootInode].size = 1;
		inode[rootInode].blockCount = 1;
		inode[rootInode].directBlock[0] = curDirBlock;

		curDir.numEntry = 1;
		strncpy(curDir.dentry[0].name, ".", 1);
		curDir.dentry[0].name[1] = '\0';
		curDir.dentry[0].inode = rootInode;
		disk_write(curDirBlock, (char*)&curDir);
	}
	return 0;
}

int fs_umount(char *name)
{
	int numInodeBlock =  (sizeof(Inode)*MAX_INODE )/ BLOCK_SIZE;
	int i, index, inode_index = 0;
	disk_write(0, (char*) &superBlock);
	disk_write(1, inodeMap);
	disk_write(2, blockMap);

	for(i = 0; i < numInodeBlock; i++)
	{
		index = i+3;
		disk_write(index, (char*) (inode+inode_index));
		inode_index += (BLOCK_SIZE / sizeof(Inode));
	}
	// current directory
	disk_write(curDirBlock, (char*)&curDir);

	disk_umount(name);	
	return 0;
}

int search_cur_dir(char *name)
{
	// return inode. If not exist, return -1
	int i;

	for(i = 0; i < curDir.numEntry; i++)
	{
		if(command(name, curDir.dentry[i].name)) return curDir.dentry[i].inode;
	}
	return -1;
}

int file_create(char *name, int size)
{
	int i;

	if(size > SMALL_FILE) {
		printf("Do not support files larger than %d bytes.\n", SMALL_FILE);
		return -1;
	}

	if(size < 0){
		printf("File create failed: cannot have negative size\n");
		return -1;
	}

	int inodeNum = search_cur_dir(name); 
	if(inodeNum >= 0) {
		printf("File create failed:  %s exist.\n", name);
		return -1;
	}

	if(curDir.numEntry + 1 > MAX_DIR_ENTRY) {
		printf("File create failed: directory is full!\n");
		return -1;
	}

	int numBlock = size / BLOCK_SIZE;
	if(size % BLOCK_SIZE > 0) numBlock++;

	if(numBlock > superBlock.freeBlockCount) {
		printf("File create failed: data block is full!\n");
		return -1;
	}

	if(superBlock.freeInodeCount < 1) {
		printf("File create failed: inode is full!\n");
		return -1;
	}

	char *tmp = (char*) malloc(sizeof(int) * size + 1);

	rand_string(tmp, size);
	printf("New File: %s\n", tmp);

	// get inode and fill it
	inodeNum = get_free_inode();
	if(inodeNum < 0) {
		printf("File_create error: not enough inode.\n");
		return -1;
	}

	inode[inodeNum].type = file;
	inode[inodeNum].owner = 1;  // pre-defined
	inode[inodeNum].group = 2;  // pre-defined
	gettimeofday(&(inode[inodeNum].created), NULL);
	gettimeofday(&(inode[inodeNum].lastAccess), NULL);
	inode[inodeNum].size = size;
	inode[inodeNum].blockCount = numBlock;
	inode[inodeNum].link_count = 1;

	// add a new file into the current directory entry
	strncpy(curDir.dentry[curDir.numEntry].name, name, strlen(name));
	curDir.dentry[curDir.numEntry].name[strlen(name)] = '\0';
	curDir.dentry[curDir.numEntry].inode = inodeNum;
	printf("curdir %s, name %s\n", curDir.dentry[curDir.numEntry].name, name);
	curDir.numEntry++;

	// get data blocks
	for(i = 0; i < numBlock; i++)
	{
		int block = get_free_block();
		if(block == -1) {
			printf("File_create error: get_free_block failed\n");
			return -1;
		}
		//set direct block
		inode[inodeNum].directBlock[i] = block;

		disk_write(block, tmp+(i*BLOCK_SIZE));
	}

	//update last access of current directory
	gettimeofday(&(inode[curDir.dentry[0].inode].lastAccess), NULL);		

	printf("file created: %s, inode %d, size %d\n", name, inodeNum, size);

	free(tmp);
	return 0;
}

int file_cat(char *name)
{
	int inodeNum, i, size;
	char str_buffer[512];
	char* str;

	//get inode
	inodeNum = search_cur_dir(name);
	size = inode[inodeNum].size;

	//check if valid input
	if(inodeNum < 0)
	{
		printf("cat error: file not found\n");
		return -1;
	}
	if(inode[inodeNum].type == directory)
	{
		printf("cat error: cannot read directory\n");
		return -1;
	}

	//allocate str
	str = (char *) malloc( sizeof(char) * (size+1) );
	str[ size ] = '\0';

	for( i = 0; i < inode[inodeNum].blockCount; i++ ){
		int block;
		block = inode[inodeNum].directBlock[i];

		disk_read( block, str_buffer );

		if( size >= BLOCK_SIZE )
		{
			memcpy( str+i*BLOCK_SIZE, str_buffer, BLOCK_SIZE );
			size -= BLOCK_SIZE;
		}
		else
		{
			memcpy( str+i*BLOCK_SIZE, str_buffer, size );
		}
	}
	printf("%s\n", str);

	//update lastAccess
	gettimeofday( &(inode[inodeNum].lastAccess), NULL );

	free(str);

	//return success
	return 0;
}

int file_read(char *name, int offset, int size)
{
	int curInode, start, end, i, block;
	char str_buffer[512];
	char* str;

	curInode = search_cur_dir(name);
	if (curInode < 0) {
		printf("Error: %s does not exist.\n", name);
		return -1;
	}
	if (inode[curInode].type == directory) {
		printf("Error: %s is a directory.\n", name);
		return -1;
	}
	
	// Calculate starting block & ending block
	start = offset / BLOCK_SIZE;
	end = min((offset + size) / BLOCK_SIZE, inode[curInode].blockCount - 1);

	// Check that start is inbounds
	if (start >= inode[curInode].blockCount) {
		printf("Error: Offset %i is larger than file.\n", offset);
		return -1;
	}

	// allocate string
	str = (char*)malloc(sizeof(char) * (size+1));

	// If starting at the middle of some block
	if (offset % BLOCK_SIZE != 0) {
		char sliced_buffer[512];
		int startOffset;
		block = inode[curInode].directBlock[start];

		disk_read( block, str_buffer );

		startOffset = (offset % BLOCK_SIZE);
		// Copy only the 'cut' we need
		strncpy(sliced_buffer, str_buffer + startOffset, 512 - startOffset);

		// Copy that into str
		memcpy(str, sliced_buffer, 512 - startOffset);
		start++;
	}

	// Read middle blocks until end
	for (i = start; i <= end; i++) {
		int block;

		block = inode[curInode].directBlock[i];

		disk_read( block, str_buffer );

		memcpy(str, str_buffer, min(BLOCK_SIZE, size));
		size -= BLOCK_SIZE;
	}

	printf("%s\n", str);
	
	// Updaet last access
	gettimeofday(&(inode[curInode].lastAccess), NULL);

	// Free dynamic memory
	free(str);

	return 0;
}


int file_stat(char *name)
{
	char timebuf[28];
	int inodeNum = search_cur_dir(name);
	if(inodeNum < 0) {
		printf("file cat error: file is not exist.\n");
		return -1;
	}

	printf("Inode\t\t= %d\n", inodeNum);
	if(inode[inodeNum].type == file) printf("type\t\t= File\n");
	else printf("type\t\t= Directory\n");
	printf("owner\t\t= %d\n", inode[inodeNum].owner);
	printf("group\t\t= %d\n", inode[inodeNum].group);
	printf("size\t\t= %d\n", inode[inodeNum].size);
	printf("link_count\t= %d\n", inode[inodeNum].link_count);
	printf("num of block\t= %d\n", inode[inodeNum].blockCount);
	format_timeval(&(inode[inodeNum].created), timebuf, 28);
	printf("Created time\t= %s\n", timebuf);
	format_timeval(&(inode[inodeNum].lastAccess), timebuf, 28);
	printf("Last acc. time\t= %s\n", timebuf);
	return 0;
}

int file_remove(char *name)
{
	int inodeNum, i;

	// Get Inode NUmber
	inodeNum = search_cur_dir(name);

	if (inodeNum < 0) {
		printf("Error: File %s not found\n", name);
		return -1;
	}
	if (inode[inodeNum].type == directory) {
		printf("Error: %s is a directory\n", name);
	}

	// Decrease link count
	inode[inodeNum].link_count--;

	// If others are linked to this, don't remove
	if (inode[inodeNum].link_count != 0) {
		gettimeofday(&(inode[inodeNum].lastAccess), NULL);
		return 0;
	}

	// Unset data bits
	for (i = 0; i < inode[inodeNum].blockCount; i++) {
		set_free_block(inode[inodeNum].directBlock[i]);
	}

	// Unset i node bit
	set_free_inode(inodeNum);

	// Remove from current directory
	for (i = 0; i < MAX_DIR_ENTRY; i++) {
		if (command(name, curDir.dentry[i].name)){
			curDir.numEntry--;
			curDir.dentry[i].name[0] = '\0';
		}
	}

	printf("File %s created.\n", name);
	return 0;
}

int dir_make(char* name)
{
	printf("Error: mkdir is not implemented.\n");
	return 0;
}

int dir_remove(char *name)
{
	printf("Error: rmdir is not implemented.\n");
	return 0;
}

int dir_change(char* name)
{
	int inodeNum, i;

	//get inode number
	inodeNum = search_cur_dir(name);
	if (inodeNum < 0) 
	{
		printf("cd error: %s does not exist\n", name);
		return -1;
	}
	if (inode[inodeNum].type != directory)
	{
		printf("cd error: %s not a directory\n", name);
		return -1;
	}

	//write parent directory (curDir) to disk
	disk_write(curDirBlock, (char*)&curDir);

	//read new directory from disk into curDir
	curDirBlock = inode[inodeNum].directBlock[0];
	disk_read(curDirBlock, (char*)&curDir);

	//update last access of directory we are changing to
	gettimeofday(&(inode[inodeNum].lastAccess), NULL);		

	return 0;
}

int ls()
{
	int i;
	for(i = 0; i < curDir.numEntry; i++)
	{
		int n = curDir.dentry[i].inode;
		if(inode[n].type == file) printf("type: file, ");
		else printf("type: dir, ");
		printf("name \"%s\", inode %d, size %d byte\n", curDir.dentry[i].name, curDir.dentry[i].inode, inode[n].size);
	}

	return 0;
}

int fs_stat()
{
	printf("File System Status: \n");
	printf("# of free blocks: %d (%d bytes), # of free inodes: %d\n", superBlock.freeBlockCount, superBlock.freeBlockCount*512, superBlock.freeInodeCount);
	return 0;
}

int hard_link(char *src, char *dest)
{
	int srcInode, destInode;
	
	// Check that src exists & not directory
	srcInode = search_cur_dir(src);
	if (srcInode < 0) {
		printf("Error: %s does not exist.\n", src);
		return -1;
	}
	if (inode[srcInode].type == directory) {
		printf("Error: %s is a directory.\n", src);
		return -1;
	}

	// Check that dest doest not exist & not directory
	destInode = search_cur_dir(dest);
	if (destInode >= 0) {
		printf("Error: %s exist.\n", dest);
		return -1;
	}
	if (inode[destInode].type == directory) {
		printf("Error: %s is a directory.\n", dest);
	}

	// Link dest
	strncpy(curDir.dentry[curDir.numEntry].name, dest, strlen(dest));
	curDir.dentry[curDir.numEntry].name[strlen(dest)] = '\0';
	curDir.dentry[curDir.numEntry].inode = srcInode;
	curDir.numEntry++;

	// Update src Inode
	inode[srcInode].link_count++;
	gettimeofday(&(inode[srcInode].lastAccess), NULL);

	printf("Created hard link:  %s-->%s\n", dest, src);
	return 0;
}

int execute_command(char *comm, char *arg1, char *arg2, char *arg3, char *arg4, int numArg)
{

    printf("\n");
	if(command(comm, "df")) {
		return fs_stat();

    // file command start    
    } else if(command(comm, "create")) {
        if(numArg < 2) {
            printf("error: create <filename> <size>\n");
            return -1;
        }
		return file_create(arg1, atoi(arg2)); // (filename, size)

	} else if(command(comm, "stat")) {
		if(numArg < 1) {
			printf("error: stat <filename>\n");
			return -1;
		}
		return file_stat(arg1); //(filename)

	} else if(command(comm, "cat")) {
		if(numArg < 1) {
			printf("error: cat <filename>\n");
			return -1;
		}
		return file_cat(arg1); // file_cat(filename)

	} else if(command(comm, "read")) {
		if(numArg < 3) {
			printf("error: read <filename> <offset> <size>\n");
			return -1;
		}
		return file_read(arg1, atoi(arg2), atoi(arg3)); // file_read(filename, offset, size);

	} else if(command(comm, "rm")) {
		if(numArg < 1) {
			printf("error: rm <filename>\n");
			return -1;
		}
		return file_remove(arg1); //(filename)

	} else if(command(comm, "ln")) {
		return hard_link(arg1, arg2); // hard link. arg1: src file or dir, arg2: destination file or dir

    // directory command start
	} else if(command(comm, "ls"))  {
		return ls();

	} else if(command(comm, "mkdir")) {
		if(numArg < 1) {
			printf("error: mkdir <dirname>\n");
			return -1;
		}
		return dir_make(arg1); // (dirname)

	} else if(command(comm, "rmdir")) {
		if(numArg < 1) {
			printf("error: rmdir <dirname>\n");
			return -1;
		}
		return dir_remove(arg1); // (dirname)

	} else if(command(comm, "cd")) {
		if(numArg < 1) {
			printf("error: cd <dirname>\n");
			return -1;
		}
		return dir_change(arg1); // (dirname)

	} else {
		fprintf(stderr, "%s: command not found.\n", comm);
		return -1;
	}
	return 0;
}

