#include <stdio.h>
#include <string.h>
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
			struct iomodeDisc* d = _out;
			if (d->isWrite==2){ // if writing has started already
				if (close(getWriteDescriptor(d->state))){
					fprintf(stderr,"failed to close the writing pipe. thats a big problem because it's the only way to signal the burning to stop, lol.\n");
					_ret=-2;
				}
				if (completeAndFreeBurn(d->state)){
					fprintf(stderr,"waitBurnComplete failed\n");
					_ret=-2;
				}
			}
			freeDrive(d->driveList);
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
		{
			struct iomodeDisc* d = _out;
			d->isWrite=2; // mark that writing has started
			if (discStartWrite(getDrive(d->driveList),1, d->state)){
				return -2;
			}
			return 0;
		}
	}
	return 0;
}
//////////////
// returns number of bytes written and -1 on error
ssize_t iomodeWrite(void* _out, char _type, const void* buffer, size_t size){
	switch(_type){
		case IOMODE_DISC:
			return write(getWriteDescriptor(((struct iomodeDisc*)_out)->state),buffer,size);
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
static size_t disciomodeRead(struct discReadState* s, char* _dest, size_t nmemb){
	if (nmemb==0){
		return 0;
	}
	if (s->buffPushed+nmemb>s->buffSize){
		size_t _ret=0;
		while (s->buffPushed+nmemb>s->buffSize){
			// push as much as possible
			size_t _canPush = s->buffSize-s->buffPushed;
			memcpy(_dest,s->buff+s->buffPushed,_canPush);
			_dest+=_canPush;
			nmemb-=_canPush;
			_ret+=_canPush;
			s->buffPushed=s->buffSize;
			// read more data
			if (s->curSector==s->maxSector){ // break because EOF
				return _ret;
			}
			int _numSectorsRead=DISCREADBUFFSEC;
			if (s->curSector+_numSectorsRead>=s->maxSector){ // if we don't have enough sectors left to fill the buffer
				_numSectorsRead=s->maxSector-s->curSector;
			}
			off_t _numRead;
			if (burn_read_data(s->drive,s->curSector*SECTORSIZE,s->buff,SECTORSIZE*_numSectorsRead,&_numRead,1)<=0){
				fprintf(stderr,"disc read error\n");
				return _ret;
			}
			s->curSector+=_numSectorsRead;
			s->buffSize=_numRead;
			s->buffPushed=0;
		}
		memcpy(_dest,s->buff+s->buffPushed,nmemb);
		_ret+=nmemb;
		s->buffPushed+=nmemb;
		return _ret;
	}else{
		memcpy(_dest,s->buff+s->buffPushed,nmemb);
		s->buffPushed+=nmemb;
		return nmemb;
	}
}
size_t iomodeRead(void* _out, char _type, void* _dest, size_t nmemb){
	switch(_type){
		case IOMODE_DISC:
			return disciomodeRead(((struct iomodeDisc*)_out)->state,_dest,nmemb);
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
signed char read64(void* _out, char _type, uint64_t* n){
	if (iomodeRead(_out,_type,n,sizeof(uint64_t))!=sizeof(uint64_t)){
		return -2;
	}
	*n=le64toh(*n);
	return 0;
}
