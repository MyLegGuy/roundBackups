/*
	Copyright (C) 2020  MyLegGuy
	This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
	You should have received a copy of the GNU General Public License along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#define IOMODE_FILE 0
#define IOMODE_DISC 1
#define IOMODE_FAKE 2

#include "disc.h"
struct iomodeDisc{
	void* state;
	char isWrite;
	struct burn_drive_info* driveList;
};
#define SECTORSIZE 2048
#define DISCREADBUFFSEC 4
struct discReadState{
	struct burn_drive* drive; // not freed, but is here to allow this to be the only arg to the functions
	size_t curSector; // the next sector to read after we finish the buffer
	size_t maxSector;
	int buffPushed; // how much of the buffer has been used
	int buffSize; // how much of the buffer is full right now
	char buff[SECTORSIZE*DISCREADBUFFSEC];
};

signed char iomodePrepareWrite(void* _out, char _type, char _leaveAppendable);
signed char iomodeClose(void* _out, char _type);
signed char iomodeInit(void** _outOut, char _requestedType);
signed char iomodeGetFree(void* _src, char _type, size_t* _retSize);
// returns number written
ssize_t iomodeWrite(void* _out, char _type, const void* buffer, size_t size);
// returns 0 if worked, -2 on fail
signed char iomodeWriteFail(void* _out, char _type, const void* buffer, size_t size);
signed char iomodePutc(void* _out, char _type, unsigned char _byte);
// works like fread but size is 1
size_t iomodeRead(void* _out ,char _type, void* _dest, size_t nmemb);
// works like fgetc
int iomodeGetc(void* _out, char _type);
///
signed char write16(void* _out, char _type, uint16_t n);
signed char write32(void* _out, char _type, uint32_t n);
signed char write64(void* _out, char _type, uint64_t n);
///
signed char read16(void* _out, char _type, uint16_t* n);
signed char read32(void* _out, char _type, uint32_t* n);
signed char read64(void* _out, char _type, uint64_t* n);
