/*
   File:
   Author: Andrija (Andy) Conkle
   Course: EECS 3540 Operating Systems, Spring 2022
   Date: 5/4/2022
   
   This file contains the main function for Lab #3 as well as all of the relevant functions to accomplish the commands that the user enters 
   into the program. 
*/
#pragma pack(1)
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>

struct BPBStruct
{
 unsigned char BS_jmpBoot[3]; // Jump instruction to boot code
 unsigned char BS_OEMNane[8]; // 8-Character string (not null terminated)
 unsigned short BPB_BytsPerSec; // Had BETTER be 512!
 unsigned char BPB_SecPerClus; // How many sectors make up a cluster?
 unsigned short BPB_RsvdSecCnt; // # of reserved sectors at the beginning (including the BPB)?
 unsigned char BPB_NumFATs; // How many copies of the FAT are there? (had better be 2)
 unsigned short BPB_RootEntCnt; // ZERO for FAT32
 unsigned short BPB_TotSec16; // ZERO for FAT32
 unsigned char BPB_Media; // SHOULD be 0xF8 for "fixed", but isn't critical for us
 unsigned short BPB_FATSz16; // ZERO for FAT32
 unsigned short BPB_SecPerTrk; // Don't care; we're using LBA; no CHS
 unsigned short BPB_NumHeads; // Don't care; we're using LBA; no CHS
 unsigned int BPB_HiddSec; // Don't care ?
 unsigned int BPB_TotSec32; // Total Number of Sectors on the volume
 unsigned int BPB_FATSz32; // How many sectors long is ONE Copy of the FAT?
 unsigned short BPB_Flags; // Flags (see document)
 unsigned short BPB_FSVer; // Version of the File System
 unsigned int BPB_RootClus; // Cluster number where the root directory starts (should be 2)
 unsigned short BPB_FSInfo; // What sector is the FSINFO struct located in? Usually 1
 unsigned short BPB_BkBootSec; // REALLY should be 6 – (sector # of the boot record backup)
 unsigned char BPB_Reserved[12]; // Should be all zeroes -- reserved for future use
 unsigned char BS_DrvNum; // Drive number for int 13 access (ignore)
 unsigned char BS_Reserved1; // Reserved (should be 0)
 unsigned char BS_BootSig; // Boot Signature (must be 0x29)
 unsigned char BS_VolID[4]; // Volume ID
 unsigned char BS_VolLab[11]; // Volume Label
 unsigned char BS_FilSysType[8]; // Must be "FAT32 "
 unsigned char unused[420]; // Not used
 unsigned char signature[2]; // MUST BE 0x55 AA
} BPB;

struct PartitionTableEntry 
{
	unsigned char bootFlag;
	unsigned char CHSBegin[3];
	unsigned char typeCode;
	unsigned char CHSEnd[3];
	unsigned int LBABegin;
	unsigned int LBALength;
}PTE;

struct MBRStruct
{
	unsigned char bootCode[446];
	struct PartitionTableEntry part1;
	struct PartitionTableEntry part2;
	struct PartitionTableEntry part3;
	struct PartitionTableEntry part4;
	unsigned short flag;
} MBR;


unsigned char SECTOR[512]; 
unsigned char SECBUF[256]; 
unsigned char DIR[32]; 
unsigned char DIR2[32];  
unsigned int TOTALFILESIZE; 
int FILECOUNT = 0; 
void Get_MBR_BPB(FILE *fd); 
unsigned int GetLSN(unsigned int N, unsigned int DataSecStart); 
unsigned int GetFAT32Entry(unsigned int CN, unsigned int FATBeginLBA, FILE *fd);

void displayFilePath(int argc, char *argv[]); 
void displayFiles(int argc, char *argv[], unsigned char* dir, unsigned char* dir2);
void displayFileDate(unsigned char *dir); 
void displayFileTime(unsigned char *dir); 
void displayFileSize(unsigned char *dir); 
void displaySFN(unsigned char *dir); 
void displayLFN(unsigned char *dir2); 
void removeSpaces(unsigned char* str); 

void displaySector(unsigned char* sector);
void displayHalfSector(unsigned char* sector); 


int main(int argc, char *argv[])
{
	
	// Take a single command line parameter - the name of the file containing the FAT32 image
	// and try to open the file; file will be in argv[1] since the executable will be in argv[0]. 
	// If the file does not exist or cannot be open; output an error message and exit. After
	// making sure that the file can be opened, prompted the user with "\>" and await for them
	// to enter 1 of 3 (required) commands: DIR, EXTRACT, QUIT. 
	FILE *fd;
	FILE *dirfd; 
	FILE *lfnfd;  
	char userInput;
	unsigned int EndofThisClus; 
	bool eof = false;    
	 
	// Open the inputed file
	fd = fopen(argv[1], "r"); 
	 
	// If the file cannot be opened, then output an error message and exit
	if(fd == NULL)
	{
		printf("File %s could not be opened.\n", argv[1]); 
		return 0; 
	}
	
	// While the user has not specified the QUIT command:
	while(strcmp(&userInput, "QUIT") != 0)
	{
		displayFilePath(argc, argv); 
		printf(">"); 
		scanf("%s", &userInput); 

		// If the user enters DIR: display the date/time of creation, size and filename (both 8.3 & LFN format) 
		// for every entry in the directory - just like the "\x" option. If the root directory is empty, display 
		// the message "File not found". 
		if(strcmp(&userInput, "DIR") == 0)
		{
	 
			Get_MBR_BPB(fd); 

			// Key information needed from the BPB
			unsigned int RsvdSectors = BPB.BPB_RsvdSecCnt; 						// # of reserved sectors
			unsigned int BytsPerSec = BPB.BPB_BytsPerSec;						// # of bytes per sector
			unsigned int FATSz32 = BPB.BPB_FATSz32;								// # of sectors per FAT
			unsigned int NumFATs = BPB.BPB_NumFATs;								// # of FATs
			unsigned int CN = BPB.BPB_RootClus;									// Root Directory Cluster #
			unsigned int FATBeginLBA = MBR.part1.LBABegin + RsvdSectors; 		// LBA of the start of first FAT
			unsigned int ClusBeginLBA = FATBeginLBA + (NumFATs * FATSz32);		// LBA of the start of first cluster

			// Find the number of root directory sectors (will be zero for FAT32). Then, find
			// the start of the data region. After, find the logical sector number for the  
			// starting root cluster. 		
			unsigned int RootDirSectors = ((BPB.BPB_RootEntCnt * 32) + (BytsPerSec - 1)) / BytsPerSec; 	// count of sectors occupied by root directory
			unsigned int DataStartSec = ClusBeginLBA + RootDirSectors;									// start of the data region 																			
			unsigned int LSN = GetLSN(CN, DataStartSec);												// Logical Sector # of the cluster stored in CN 
			unsigned int RootStartLSN = LSN;

			// Seek to the LSN and read in the contents (512-bytes) into the sector array
			fseek(fd, LSN * 512, SEEK_SET);	
			fread(SECTOR, 512, 1, fd);	// The fd here will point to the end of the first cluster of the root directory.  	
			dirfd = fd; 
			EndofThisClus = ftell(fd);

			// We know this clusters contents is the start of the root directory. Now, we
			// want to read in and output information about each of the files using the 
			// attributes found in each of the 32-byte entries. 
			fseek(dirfd, RootStartLSN * 512, SEEK_SET);
			unsigned int Currfd = ftell(dirfd); 

			// Keep reading the 32 bytes entries until we get to the end of the current
			// cluster. 
			while(Currfd != EndofThisClus)
			{
				fread(DIR, 32, 1, dirfd);
				fread(DIR2, 32, 1, dirfd); // The long file name is before the sfn
				unsigned char attr = DIR[11] &0x3F; 
				if((attr&0x02) == 0x02) { continue; }
				else { displayFiles(argc, argv, DIR, DIR2); } 
				Currfd = Currfd + 32; 
			}

			// We have now reach the end of the first root cluster. Need to check the FAT 
			// and see if there are any other chains in this cluster. If there are, then
			// that means that we still have files that we need to display.
			// Find the offset for this cluster. Then find the sector number for this FAT
			// followed by the entry offset. After, seek to the sector number found and 
			// read in the contents into SecBuf array while we haven't reached the eof. 
			while(eof != true)
			{
				// Get the contents of the cluster at the specified offset
				unsigned NextClusEntry = GetFAT32Entry(CN, FATBeginLBA, fd);

				// If the value stored at that location is greater than 0x0FFFFFF8,
				// then we have reach the EOC. Break out of the loop. 
				if(NextClusEntry >= 0x0FFFFFF8)
				{
					eof = true; 
					break; 
				}

				// Set the cluster number and then find the logical sector number. 
				// Seek to this sector and read in the 512 bytes stored there. 
				CN = NextClusEntry; 
				LSN = GetLSN(CN, DataStartSec); 
				fseek(fd, LSN * 512, SEEK_SET); 
				fread(SECTOR, 512, 1, fd); 
				EndofThisClus = ftell(fd); 
				fseek(dirfd, LSN * 512, SEEK_SET); 
				Currfd = ftell(dirfd); 

				while(Currfd != EndofThisClus)
				{
					fread(DIR, 32, 1, dirfd);
					fread(DIR2, 32, 1, dirfd);
					unsigned char attr = DIR[11] &0x3F; 
					if((attr&0x02) == 0x02) { continue; }
					else { displayFiles(argc, argv, DIR, DIR2); } 
					Currfd = Currfd + 32; 
				}
			}
			printf("%15d File(s) %d bytes\n",FILECOUNT, TOTALFILESIZE); 
		}
		
		// If the user enters EXTRACT <filename>: Copy the contents of the file OUTSIDE of the ".img" file. If the
		// file does not exist, then display the message "File not found". If the file does exit, create <filename> 
		// in the same folder as the executable. This command is basically copy from INSIDE the image to OUTSIDE the
		// image. 

	}  
	fclose(fd); 
	return 0; 
}

void Get_MBR_BPB(FILE *fd)
{
	fread(&MBR, 512, 1, fd); 							// Read the 512-bytes of the sector into the MBR struct
	fseek(fd, MBR.part1.LBABegin * 512, SEEK_SET);		// Go to the start of the FAT32 Partition  
	fread(&BPB, 512, 1, fd); 							// Read the BPB and save key information
}

// This function will first calculate the offset for the cluster number specified
// by CN. Then it will obtain the sector number and offset for this cluster and 
// finally it will read the contents specified by ThisFATSecNum and ThisFATEntOffset
// and store it inside FAT32ClusEntVal.
unsigned int GetFAT32Entry(unsigned int CN, unsigned int FATBeginLBA, FILE *fd)
{

	// Calculate the offset, sector number and entry offset specified by the 
	// cluster #
	unsigned int FATOffset = CN * 4;														
	unsigned int ThisFATSecNum = FATBeginLBA + (FATOffset / BPB.BPB_BytsPerSec);													
	unsigned int ThisFATEntOffset = FATOffset % BPB.BPB_BytsPerSec; 							

	// Go the the location specified by ThisFATSecNum and read in the 256 bytes into
	// the SecBuf array. 
	fseek(fd, ThisFATSecNum * 512, SEEK_SET); 
	fread(SECBUF, 256, 1, fd); 

	// Get the 32 bit value stored inside the SecBuf array and mask the high 4 bits
	unsigned int FAT32ClusEntVal = (*((unsigned int*) &SECBUF[ThisFATEntOffset])) & 0x0FFFFFFF;
	return FAT32ClusEntVal; 
}


// This function obtains the logical sector number for any given cluster 
// number N. 
unsigned int GetLSN(unsigned int N, unsigned int DataSecStart)
{
	unsigned int SecPerClus = BPB.BPB_SecPerClus; 
	unsigned int LogicalSecNum = ((N - 2) * SecPerClus) + DataSecStart; 
	return LogicalSecNum; 
}

void displayFilePath(int argc, char *argv[])
{ 
	for(int i = 0; i < argc - 1; i++) { printf("%s", argv[i]); }
}

void displayFiles(int argc, char *argv[], unsigned char* dir, unsigned char* dir2)
{ 
	if(dir[11] == 0x08)
	{
		// Display the name of the volume in the image file
		printf("Volume in image file is ");
		for(int i = 0; i < 12; i++) { printf("%lc", dir[i]); }

		// Display the volume ID from the BPB
		printf("\nVolume ID is "); 
		for(int i = 0; i < 4; i ++) 
		{ 
			printf("%d", BPB.BS_VolID[i]); 
		}
		printf("\n\n"); 

		// Display the file path
		printf("Directory of "); 
		displayFilePath(argc, argv); 
		printf("\n\n"); 
		return; 
	}
	else
	{
		// Display the date of creation, time of creation, size of the file in
		// bytes, 8.3 filename and then the Long filename. 
		displayFileDate(dir); 
		displayFileTime(dir); 
		displayFileSize(dir); 
		displaySFN(dir); 
		displayLFN(dir2);
		printf("\n"); 
	}	
}

void displaySFN(unsigned char *dir)
{
	unsigned char sfn[12] = "";
	int extensionStart = 9; 

	for(int i = 0; i < 8; i++)
	{
		// We we find a space, that means we have reached the end of the filename
		// break out of the loop. 
		if(dir[i] == 0x20) 
		{ 
			sfn[i] = 0x2E;
			extensionStart = i + 1; 
			break; 					// break out of the loop
		}

		sfn[i] = dir[i]; 
	} 
	
	if(extensionStart == 9) { sfn[8] = 0x2E; }

	// Now, add the extension portion of the file after the newly added "."
	for(int i = 8; i <= 10; i++)
	{
		if(dir[i] == 0x20) { continue; }
		// Start adding the extension name to start after the "."
		// Increment the index of the SFN array. 
		sfn[extensionStart] = dir[i];
		extensionStart++;  
	}
	
	// Print out the short filename. 
	printf("%s", sfn); 
}

void displayLFN(unsigned char *dir2)
{
	unsigned char lfn[32] = " ";
	for(int i = 1; i <= 10; i++)
	{
		if(dir2[i] == 0) { lfn [i] = 0x20; continue; }
		if(dir2[i] == 0xFF) { break; }
		lfn[i] = dir2[i]; 
	}

	for(int i = 11; i < 14; i++) { lfn[i] = 0x20; }

	for(int i = 14; i < 26; i++)
	{
		if(dir2[i] == 0) { lfn[i] = 0x20; continue; }
		if(dir2[i] == 0xFF) { break; }
		lfn[i] = dir2[i]; 
	}

	for(int i = 28; i < 32; i++)
	{
		if(dir2[i] == 0) { lfn[i] = 0x20; continue; }
		if(dir2[i] == 0xFF) { break; }
		lfn[i] = dir2[i]; 
	}
	removeSpaces(lfn); 
	printf(" %s", lfn);  
}

/*
               24                16                 8                 0
+-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+
|Y|Y|Y|Y|Y|Y|Y|M| |M|M|M|D|D|D|D|D| |h|h|h|h|h|m|m|m| |m|m|m|s|s|s|s|s|
+-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+
 \___________/\________/\_________/ \________/\____________/\_________/
    year        month       day      hour       minute        second

*/

void removeSpaces(unsigned char* str)
{

	int i, j = 0; 

	for(i = 0; str[i] != '\0'; i++)
	{
		str[i] = str[i + j]; 
		if(str[i] == ' ' || str[i] == '\t')
		{ 
			j++; 
			i--;
		}
	}
	
}

void displayFileDate(unsigned char *dir)
{
	// Figure out the date of creation of the file 
	unsigned char y = ((dir[25]&0xFE) >> 1);
	int year = (int) y + 1980;
	unsigned char m = (dir[24]&0xE0) >> 5 | (dir[25]&0x01) << 3;
	int month = (int) m; 
	unsigned char d = dir[24]&0x1F; 
	int day = (int) d; 
	printf("%.2d/%.2d/%.4d ", month, day, year); 
}

void displayFileTime(unsigned char *dir)
{
	// Figure out the time of creation for the file
	unsigned int hour = (dir[23]&0xF8) >> 3; 
	unsigned char minute = (dir[23]&0x07) << 3 | (dir[22]&0xE0) >> 5; 
	unsigned char second = (dir[22]&0x1F) << 1; 
	char meridien[2] = ""; 

	if(hour < 12) { meridien[0] = 0x41; meridien[1] = 0x4D; }
	else { meridien[0] = 0x50; meridien[1] = 0x4D; }
	
	hour = hour % 12; 
	if(hour == 0) 
	{ 
		printf("12:%.2d:%.2d %c%c", minute, second, meridien[0], meridien[1]);  
	}
	else 
	{
		printf("%d:%.2d:%.2d %c%c", hour, minute, second, meridien[0], meridien[1]); 
	}
}

void displayFileSize(unsigned char *dir)
{
	unsigned int FileSize = (dir[28]) + (dir[29] << 8) + (dir[30] << 16) + (dir[31] << 24); 
	printf("%10i ", FileSize);
	TOTALFILESIZE = TOTALFILESIZE + FileSize;
	FILECOUNT++;   
}