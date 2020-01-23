#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <gpgme.h>
#include <zlib.h>

#define TOPBACKUPBYTES 100
struct headerBak{
	/*
	Format:
	<packet header>
	<0x00 or 0x01. if 0x01 then packet data is after this. otherwise another packet header is coming.>
	*/
	char* data;
	uint64_t size;

	char numBuff[4]; // for 5 byte numbers
	signed char headerProgress; // -1 if done. need to use like that because curLeft can be 0 after valid packet
	signed char packetType; // if 0 then regular new packet. if -1 then partial length new packet. if > 0 then it's an old packet and the number is the number of length bytes.
	uint64_t curLeft;
	char saveThisContent;
};
struct info{
	struct headerBak hInfo;
	uLong curHash;
	
};

void processHeaderBak(struct headerBak* h, const unsigned char* buff, size_t size){
	// TODO - Save bits. if h->curLeft then save what you're not parsing. if we're going to go over to the next header, don't save that data yet. in the header thing, save the current byte at the end of the switch statemrnt
top:
	if (!h->curLeft){
		unsigned char _curByte=buff[0];
		switch(h->headerProgress){
			case 0: // tag byte
			{
				if (h->packetType==-1){ // for the next partial length header, don't read tag byte 0
					h->headerProgress=1;
					h->packetType=0;
					goto top;
				}
				unsigned char _gottenTag;
				if (!(_curByte & 64)){ // old packet format
					_gottenTag =((unsigned char)(_curByte<<2))>>4;
					h->packetType=(((unsigned char)(_curByte<<6))>>6)+1;
					if (h->packetType==4){ // (actually 3, but we added 1.)
						fprintf(stderr,"indeterminate packet length in old packet format unsupported. why is this being used? is this even a packet header?\n");
						exit(1);
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
					exit(1);
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
		}else{
			h->headerProgress++;
		}
		++buff;
		if (--size!=0){
			goto top;
		}
	}else{ // just doing more of the packet. check for the end.
		if (size>h->curLeft){
			buff+=h->curLeft;
			size-=h->curLeft;
			h->curLeft=0;
			goto top;
		}else{
			h->curLeft-=size;
		}
	}
}
ssize_t myWrite(void *handle, const void *buffer, size_t size){
	struct info* _myInfo = handle;
	processHeaderBak(&_myInfo->hInfo,buffer,size);
	
	_myInfo->curHash = crc32(_myInfo->curHash,buffer,size);
	int i;
	for (i=0;i<size;++i){
		//fputc(((char*)buffer)[i],stdout);
	}
	return size;
}
void initHeaderBak(struct headerBak* h){
	h->data=NULL;
	h->size=0;
	h->curLeft=0;
	h->headerProgress=0;
}
void initInfo(struct info* i){
	i->curHash=crc32(0L, Z_NULL, 0);
	initHeaderBak(&i->hInfo);
}
//////////////
int left=500;
ssize_t myRead(void *handle, void *buffer, size_t size){
	if (!left){
		return 0;
	}
	if (size>left){
		size=left;
	}
	left-=size;
	memset(buffer,0,size);
	return size;
}

gpgme_error_t myGetPassword(void *hook, const char *uid_hint, const char *passphrase_info, int prev_was_bad, int fd){
	printf("called b\n");
	char* pass = "aaa\n";
	if (gpgme_io_writen(fd,pass,strlen(pass)+1)){
		return 1; // TODO - another error code
	}
	return GPG_ERR_NO_ERROR;
}
//////////////
void failIfError(gpgme_error_t e, const char* _preMessage){
	if (e!=GPG_ERR_NO_ERROR){
		fprintf(stderr,"%s: %s\n",_preMessage ? _preMessage : "GPGme Error: ",gpgme_strerror(e));
		exit(1);
	}
}
int main(int argc, char** args){
	fprintf(stderr,"version: %s\n",gpgme_check_version(NULL));
	failIfError(gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP),"openpgp engine check");

	struct info _myInfo;
	initInfo(&_myInfo);
	
	void* _myHandle=&_myInfo;
	// must stay around in memory: https://github.com/gpg/gpgme/blob/b182838f71d8349d6cd7be9ecfb859b893d09127/src/data-user.c#L101
	struct gpgme_data_cbs _inCallbacks;
	_inCallbacks.read=myRead;
	_inCallbacks.write=NULL;
	_inCallbacks.seek=NULL;
	_inCallbacks.release=NULL;
	struct gpgme_data* _myIn;
	failIfError(gpgme_data_new_from_cbs(&_myIn,&_inCallbacks, _myHandle),"in gpgme_data_new_from_cbs");

	struct gpgme_data_cbs _outCallbacks;
	_outCallbacks.read=NULL;
	_outCallbacks.write=myWrite;
	_outCallbacks.seek=NULL;
	_outCallbacks.release=NULL;
	struct gpgme_data* _myOut;
	failIfError(gpgme_data_new_from_cbs(&_myOut,&_outCallbacks, _myHandle),"out gpgme_data_new_from_cbs");

	struct gpgme_context* _myContext;
	failIfError(gpgme_new(&_myContext),"gpgme_new");
	failIfError(gpgme_set_protocol(_myContext,GPGME_PROTOCOL_OpenPGP),"gpgme_set_protocol");
	gpgme_set_offline(_myContext,1);
	gpgme_set_passphrase_cb(_myContext,myGetPassword,_myHandle);

	failIfError(gpgme_op_encrypt(_myContext,NULL, GPGME_ENCRYPT_NO_COMPRESS | GPGME_ENCRYPT_SYMMETRIC,_myIn,_myOut),"gpgme_op_encrypt");
	// TODO - write the rest of the metadata to the file, starting with 0x00
	
	gpgme_release(_myContext);
}
