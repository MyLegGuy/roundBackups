#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <gpgme.h>
#include <zlib.h>
#include <sys/stat.h>
#include <endian.h>
#include <woarcassemble.h>

#define METADATAMAGIC "ROUNDEND"

#define TOPBACKUPBYTES 100
struct headerBak{
	/*
	Format:
	<packet header>
	<0x00 or 0x01. if 0x01 then packet data is after this. otherwise another packet header is coming.>
	*/
	char* data;
	uint64_t curUsed;
	uint64_t size;

	unsigned char numBuff[4]; // for 5 byte numbers
	signed char headerProgress; // -1 if done. need to use like that because curLeft can be 0 after valid packet
	signed char packetType; // if 0 then regular new packet. if -1 then partial length new packet. if > 0 then it's an old packet and the number is the number of length bytes.
	uint64_t curLeft;
	char saveThisContent;
};
struct info{
	struct headerBak hInfo;
	uLong curHash;
	struct compressState* assembleState;
	char archiveDoneMaking;
	//
	FILE* testout;
};
struct fileCallInfo{
	char** filenames; // absolute filenames
	char* rootDir; // ends in a slash. chop everything off the filename past this. 
};

// save all of this
#define REALLOCINCREMENT 50
signed char addToSave(struct headerBak* h, const unsigned char* buff, size_t addNum){
	uint64_t _destUsed=h->curUsed+addNum;
	if (_destUsed>=h->size){
		
		uint64_t _newSize=h->size+REALLOCINCREMENT;
		if (_destUsed>=_newSize){
			_newSize=_destUsed+REALLOCINCREMENT;
		}
		h->size=_newSize;
		if (!(h->data = realloc(h->data,h->size))){
			return -2;
		}
	}	
	memcpy(h->data+h->curUsed,buff,addNum);
	h->curUsed=_destUsed;
	return 0;
}
signed char processHeaderBak(struct headerBak* h, const unsigned char* buff, size_t size){
top:
	if (!h->curLeft){
		unsigned char _curByte=buff[0];
	beforeswitch:
		switch(h->headerProgress){
			case 0: // tag byte
			{
				if (h->packetType==-1){ // for the next partial length header, don't read tag byte 0
					h->headerProgress=1;
					h->packetType=0;
					goto beforeswitch;
				}
				unsigned char _gottenTag;
				if (!(_curByte & 64)){ // old packet format
					_gottenTag =((unsigned char)(_curByte<<2))>>4;
					h->packetType=(((unsigned char)(_curByte<<6))>>6)+1;
					if (h->packetType==4){ // (actually 3, but we added 1.)
						fprintf(stderr,"indeterminate packet length in old packet format unsupported. why is this being used? is this even a packet header?\n");
						return -2;
					}
				}else{ // new packet format
					_gottenTag=((unsigned char)(_curByte<<2))>>2;
					h->packetType=0;
				}
				h->saveThisContent=!(_gottenTag==18);
			}
				break;
			case 1:
				h->numBuff[0]=_curByte;
				if (h->packetType==0){ // new packet type
					if (_curByte<=191){ // One-Octet Lengths
						h->curLeft=_curByte;
						h->headerProgress=-1;
					}else if (_curByte>=224 && _curByte<255){ // partial packet
						h->packetType=-1;
						h->curLeft=1<<(_curByte & 0x1F);
						h->headerProgress=-1;
					}
				}
				break;
			case 2:
				h->numBuff[1]=_curByte;
				if (h->packetType==0){ // new packet type
					if (h->numBuff[0]>=192 && h->numBuff[0]<=223){ // Two-Octet Lengths
						h->curLeft=((h->numBuff[0]-192)<<8)+192+h->numBuff[1];
						h->headerProgress=-1;
					}
				}
				break;
			case 3:
			case 4:
				h->numBuff[h->headerProgress-1]=_curByte;
				break;
			case 5:
				if (h->numBuff[0]!=255){
					fprintf(stderr,"first byte isn't 255. is %d. bad five-octet length!\n",h->numBuff[0]);
					return -2;
				}
				h->curLeft=(h->numBuff[1] << 24) | (h->numBuff[2] << 16) | (h->numBuff[3] << 8)  | _curByte;
				h->headerProgress=-1;
				break;
		}
		// finish up old packet style length if needed
		if (h->packetType!=0 && h->headerProgress==h->packetType){
			int i;
			for (i=0;i<h->packetType;++i){
				h->curLeft|=(h->numBuff[i]<<((h->packetType-1-i)*8));
			}
			h->headerProgress=-1;
		}
		// let's go again!
		if (h->headerProgress==-1){ // just finished, reset vars for next time
			//fprintf(stderr,"length is:  %ld\n",h->curLeft);
			h->headerProgress=0;
			// write the byte that tels if packet data follows
			unsigned char _nextIsPacketData=h->saveThisContent;
			addToSave(h,&_nextIsPacketData,1);
		}else{
			h->headerProgress++;
		}
		if (addToSave(h,buff,1)==-2){
			return -2;
		}
		++buff;
		if (--size!=0){
			goto top;
		}
	}else{ // just doing more of the packet. check for the end.
		if (size>h->curLeft){
			if (h->saveThisContent && addToSave(h,buff,h->curLeft)==-2){
				return -2;
			}
			buff+=h->curLeft;
			size-=h->curLeft;
			h->curLeft=0;
			goto top;
		}else{
			if (h->saveThisContent && addToSave(h,buff,size)==-2){
				return -2;
			}
			h->curLeft-=size;
		}
	}
	return 0;
}
ssize_t myWrite(void *handle, const void *buffer, size_t size){
	struct info* _myInfo = handle;
	if (processHeaderBak(&_myInfo->hInfo,buffer,size)==-2){
		errno=EIO;
		return -1;
	}
	
	_myInfo->curHash = crc32_z(_myInfo->curHash,buffer,size);
	int i;
	for (i=0;i<size;++i){
		fputc(((char*)buffer)[i],_myInfo->testout);
	}
	return size;
}
// return n bytes read, 0 on EOF, -1 on error
ssize_t myRead(void *handle, void *buffer, size_t size){
	struct info* _myInfo = handle;
	if (_myInfo->archiveDoneMaking){
		return 0;
	}
	size_t gotBytes;
	switch(makeMoreArchive(_myInfo->assembleState,buffer,size,&gotBytes)){
		case -1:
			_myInfo->archiveDoneMaking=1;
			break;
		case -2:
			fprintf(stderr,"makeMoreArchive returned error\n");
			return -1;
	}
	return gotBytes;
}
void initHeaderBak(struct headerBak* h){
	h->data=NULL;
	h->curUsed=0;
	h->size=0;
	h->curLeft=0;
	h->headerProgress=0;
}
void initInfo(struct info* i){
	i->curHash=crc32(0L, Z_NULL, 0);
	i->archiveDoneMaking=0;
	initHeaderBak(&i->hInfo);
}
void failIfError(gpgme_error_t e, const char* _preMessage){
	if (e!=GPG_ERR_NO_ERROR){
		fprintf(stderr,"%s: %s\n",_preMessage ? _preMessage : "GPGme Error: ",gpgme_strerror(e));
		exit(1);
	}
}
//////////////
/* gpgme_error_t myGetPassword(void *hook, const char *uid_hint, const char *passphrase_info, int prev_was_bad, int fd){ */
/* 	printf("called b\n"); */
/* 	char* pass = "aaa\n"; */
/* 	if (gpgme_io_writen(fd,pass,strlen(pass)+1)){ */
/* 		return 1; // TODO - another error code */
/* 	} */
/* 	return GPG_ERR_NO_ERROR; */
/* } */
//////////////
signed char write16(FILE* fp, uint16_t n){
	n = htole16(n);
	return fwrite(&n,1,sizeof(uint16_t),fp)!=sizeof(uint16_t) ? -2 : 0;
}
signed char write32(FILE* fp, uint32_t n){
	n = htole32(n);
	return fwrite(&n,1,sizeof(uint32_t),fp)!=sizeof(uint32_t) ? -2 : 0;
}
signed char write64(FILE* fp, uint64_t n){
	n = htole64(n);
	return fwrite(&n,1,sizeof(uint64_t),fp)!=sizeof(uint64_t) ? -2  : 0;
}
//////////////
signed char getFileSize(FILE* fp, size_t* ret){
	struct stat st;
	if (fstat(fileno(fp),&st)){
		return -2;
	}
	*ret=st.st_size;
	return 0;
}
signed char woarcInitSource(size_t i, struct fileMeta* infoDest, void** srcDest, void* _userData){
	if (!(*srcDest=fopen(((struct fileCallInfo*)_userData)->filenames[i],"rb"))){
		return -2;
	}
	size_t _gotLen;
	if (getFileSize(*srcDest,&_gotLen)){
		return -2;
	}
	infoDest->len=_gotLen;
	infoDest->lastModified=0;
	return 0;
}
signed char woarcCloseSource(size_t i, void* _closeThis, void* _userData){
	return fclose(_closeThis)==0 ? 0 : -2;
}
signed char woarcGetFilename(size_t i, char** dest, void* _userData){
	struct fileCallInfo* _info = _userData;
	*dest=_info->filenames[i]+strlen(_info->rootDir);
	return 0;
}
signed char woarcGetComment(size_t i, char** dest, void* _userData){
	*dest="";
	return 0;
}
signed char woarcReadData(void* src, char* dest, size_t requested, size_t* actual){
	*actual = fread(dest,1,requested,src);
	if (*actual!=requested){
		if (ferror(src)){
			return -2;
		}else if (feof(src)){
			return -1;
		}
	}
	return 0;
}
//////////////
#define TESTCOUNT 3
int main(int argc, char** args){
	// init compress state
	char* fns[TESTCOUNT]={
		"/tmp/a",
		"/tmp/b",
		"/tmp/c",
	};
	// init archive maker
	struct fileCallInfo* i = malloc(sizeof(struct fileCallInfo));
	i->filenames=fns;
	i->rootDir="/tmp/";
	struct compressState* s = newState(TESTCOUNT);
	struct userCallbacks* c = getCallbacks(s);
	c->userData=i; // see the usage of the passed userdata in the source open functions
	c->initSourceFunc=woarcInitSource;
	c->closeSourceFunc=woarcCloseSource;
	c->getFilenameFunc=woarcGetFilename;
	c->getCommentFunc=woarcGetComment;
	c->getSourceData=woarcReadData;

	// init gpg
	fprintf(stderr,"version: %s\n",gpgme_check_version(NULL));
	failIfError(gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP),"openpgp engine check");
	
	// init pass info
	struct info _myInfo;
	initInfo(&_myInfo);
	_myInfo.assembleState=s;
	void* _myHandle=&_myInfo;
	_myInfo.testout = fopen("/tmp/arcout","wb");
	// init in
	// must stay around in memory: https://github.com/gpg/gpgme/blob/b182838f71d8349d6cd7be9ecfb859b893d09127/src/data-user.c#L101
	struct gpgme_data_cbs _inCallbacks;
	_inCallbacks.read=myRead;
	_inCallbacks.write=NULL;
	_inCallbacks.seek=NULL;
	_inCallbacks.release=NULL;
	struct gpgme_data* _myIn;
	failIfError(gpgme_data_new_from_cbs(&_myIn,&_inCallbacks, _myHandle),"in gpgme_data_new_from_cbs");
	// init out
	struct gpgme_data_cbs _outCallbacks;
	_outCallbacks.read=NULL;
	_outCallbacks.write=myWrite;
	_outCallbacks.seek=NULL;
	_outCallbacks.release=NULL;
	struct gpgme_data* _myOut;
	failIfError(gpgme_data_new_from_cbs(&_myOut,&_outCallbacks, _myHandle),"out gpgme_data_new_from_cbs");
	// init context
	struct gpgme_context* _myContext;
	failIfError(gpgme_new(&_myContext),"gpgme_new");
	failIfError(gpgme_set_protocol(_myContext,GPGME_PROTOCOL_OpenPGP),"gpgme_set_protocol");
	gpgme_set_offline(_myContext,1);
	//gpgme_set_passphrase_cb(_myContext,myGetPassword,_myHandle);
	// Do the encryption
	if(gpgme_op_encrypt(_myContext,NULL, GPGME_ENCRYPT_NO_COMPRESS | GPGME_ENCRYPT_SYMMETRIC,_myIn,_myOut)!=GPG_ERR_NO_ERROR){
		fprintf(stderr,"gpgme_op_encrypt error\n");
		exit(1);
	}
	gpgme_release(_myContext);
	// write the rest of the file
	if (fputc(0,_myInfo.testout)==EOF){
		fprintf(stderr,"io error\n");
	}
	if (fwrite(METADATAMAGIC,1,strlen(METADATAMAGIC),_myInfo.testout)!=strlen(METADATAMAGIC) ||
		write32(_myInfo.testout,_myInfo.curHash)==-2 ||
		write64(_myInfo.testout,_myInfo.hInfo.curUsed)==-2 ||
		fwrite(_myInfo.hInfo.data,1,_myInfo.hInfo.curUsed,_myInfo.testout)!=_myInfo.hInfo.curUsed){

		fprintf(stderr,"write error\n");
	}
}
