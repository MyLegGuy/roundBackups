#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "iomode.h"
//////////////
signed char iomodeGetFree(void* _src, char _type, size_t* _retSize){
	switch(_type){
		case IOMODE_DISC:
			*_retSize=getDiscFreeSpace(getDrive(((struct iomodeDisc*)_src)->driveList));
			return 0;
		case IOMODE_FILE:
			*_retSize=1000000000;
			return 0;
	}
	return -2;
}
signed char iomodeClose(void* _out, char _type){
	switch (_type){
		case IOMODE_DISC:
		{
			signed char _ret=0;
			if (close(getWriteDescriptor(_out))){
				fprintf(stderr,"failed to close the writing pipe. thats a big problem because it's the only way to signal the burning to stop, lol.\n");
				_ret=-2;
			}
			if (completeAndFreeBurn(_out)){
				fprintf(stderr,"waitBurnComplete failed\n");
				_ret=-2;
			}
			freeDrive(((struct iomodeDisc*)_out)->driveList);
			return _ret;
		}
		case IOMODE_FILE:
			return fclose(_out)==0 ? 0 : -2;
	}
	return -2;
}
// returns -2 if failed.
// call only once.
// the initDiscLib call is not here because the way that iomodeInit initializes a new iomode out parameter not the iomode internals themselves.
signed char iomodeInit(void** _outOut, char _requestedType){
	switch(_requestedType){
		case IOMODE_DISC:
		{
			*_outOut = malloc(sizeof(struct iomodeDisc));
			return (*_outOut==NULL) ? -2 : 0;
		}
		case IOMODE_FILE:
			return 0;			
	}
	return -2;
}
// returns -2 if failed
signed char iomodePrepareWrite(void* _out, char _type){
	switch(_type){
		case IOMODE_DISC:
			if (discStartWrite(getDrive(((struct iomodeDisc*)_out)->driveList),1, _out)){
				return -2;
			}
			return 0;
	}
	return 0;
}
//////////////
// returns number of bytes written and -1 on error
ssize_t iomodeWrite(void* _out, char _type, const void* buffer, size_t size){
	switch(_type){
		case IOMODE_DISC:
			return write(getWriteDescriptor(_out),buffer,size);
		case IOMODE_FILE:
			return fwrite(buffer,1,size,_out);
	}
	return -1;
}
signed char iomodeWriteFail(void* _out, char _type, const void* buffer, size_t size){
	return iomodeWrite(_out,_type,buffer,size)==size ? 0 : -2;
}
// returns 0 if worked, -2 on fail
signed char iomodePutc(void* _out, char _type, unsigned char _byte){
	return iomodeWriteFail(_out,_type,&_byte,1);
}
size_t iomodeRead(void* _out, char _type, void* _dest, size_t nmemb){
	switch(_type){
		case IOMODE_DISC:
			fprintf(stderr,"disc read body not ready yet\n");
			// TODO
			break;
		case IOMODE_FILE:
			return fread(_dest,1,nmemb,_out);
	}
	return 0;
}
int iomodeGetc(void* _out, char _type){
	unsigned char _ret;
	if (iomodeRead(_out,_type,&_ret,1)!=1){
		return EOF;
	}
	return _ret;
}
//////////////
signed char write16(void* _out, char _type, uint16_t n){
	n = htole16(n);
	return iomodeWriteFail(_out,_type,&n,sizeof(uint16_t));
}
signed char write32(void* _out, char _type, uint32_t n){
	n = htole32(n);
	return iomodeWriteFail(_out,_type,&n,sizeof(uint32_t));
}
signed char write64(void* _out, char _type, uint64_t n){
	n = htole64(n);
	return iomodeWriteFail(_out,_type,&n,sizeof(uint64_t));
}
//////////////
signed char read32(void* _out, char _type, uint32_t* n){
	if (iomodeRead(_out,_type,n,sizeof(uint32_t))!=sizeof(uint32_t)){
		return -2;
	}
	*n=le32toh(*n);
	return 0;
}
