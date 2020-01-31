#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "iomode.h"
// returns number of bytes written and -1 on error
ssize_t iomodeWrite(void* _out, char _type, const void* buffer, size_t size){
	switch(_type){
		case IOMODE_DISC:
			// TODO
			break;
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
signed char iomodeClose(void* _out, char _type){
	switch (_type){
		case IOMODE_DISC:
			break;
		case IOMODE_FILE:
			return fclose(_out)==0 ? 0 : -2;
	}
	return -2;
}
size_t iomodeRead(void* _out, char _type, void* _dest, size_t nmemb){
	switch(_type){
		case IOMODE_DISC:
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
