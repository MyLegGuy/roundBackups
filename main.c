/*
	Copyright (C) 2020  MyLegGuy
	This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
	You should have received a copy of the GNU General Public License along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
// note: https://www.gnu.org/software/libc/manual/html_node/Cleanups-on-Exit.html
// TODO - check permissions at the start for all passed files
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <gpgme.h>
#include <zlib.h>
#include <sys/stat.h>
#include <endian.h>

#include "config.h"
#include "verify.h"
#include "iomode.h"
#include "userIn.h"
#include <woarcassemble.h>
#include "borrowed/goodLinkedList.h"
#include "borrowed/filter.h"
#include "roundFormat.h"
#include "main.h"
#include "newFileGetter.h"

#define TOPBACKUPBYTES 100
struct headerBak{
	/*
	Format:
	<packet header>
	<0x00 or 0x01. if 0x01 then packet data is after this. otherwise another packet header is coming.>
	*/
	unsigned char* data;
	uint64_t curUsed;
	uint64_t size;
	signed char isCompress; // 0 if impossible. 1 is true. -1 if can compress.

	unsigned char numBuff[4]; // for 5 byte numbers
	signed char headerProgress; // -1 if done. need to use like that because curLeft can be 0 after valid packet
	signed char packetType; // if 0 then regular new packet. if -1 then partial length new packet. if > 0 then it's an old packet and the number is the number of length bytes.
	uint64_t curLeft;
	char saveThisContent;
};
struct gpgInfo{
	struct headerBak hInfo;
	uLong curHash;
	struct compressState* assembleState;
	char archiveDoneMaking;
	//
	char iomode;
	void* out;
};
struct fileCallInfo{
	struct newFile** fileList;
	char* rootDir; // ends in a slash. chop everything off the filename past this. 
};

//////////////////
// returns 1 if line is empty
char removeNewline(char* _toRemove){
	int _cachedStrlen = strlen(_toRemove);
	if (_cachedStrlen==0){
		return 1;
	}
	if (_toRemove[_cachedStrlen-1]==0x0A){ // Last char is UNIX newline
		if (_cachedStrlen>=2 && _toRemove[_cachedStrlen-2]==0x0D){ // If it's a Windows newline
			if (_cachedStrlen==2){
				return 1;
			}
			_toRemove[_cachedStrlen-2]='\0';
		}else{ // Well, it's at very least a UNIX newline
			if (_cachedStrlen==1){
				return 1;
			}
			_toRemove[_cachedStrlen-1]='\0';
		}
	}
	return 0;
}
//////////////////
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
	// NOTE - be very careful to apply any changes from here to verifyDisc function
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
					}else if (h->packetType==3){
						h->packetType=4;
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
		if (h->headerProgress==-1){ // just finished, reset vars for next time
			if (h->packetType==-1){ // if it's a partial length packet then try to compress
			switchcompresstype:
				switch(h->isCompress){
					case -1:
						if (_curByte==h->data[h->curUsed-2]){ // init the compress if it's the same byte
							// add an additional byte for the second byte of the number
							if (addToSave(h,buff,1)==-2){
								return -2;
							}
							h->data[h->curUsed-2]=0x80; // set leftmost bit of first byte
							h->data[h->curUsed-1]=2; // this is our second header in a row
							h->isCompress=1;
							break;
						}else{
							h->isCompress=0; // start a new potential compress
							goto switchcompresstype;
						}
					case 0: // just add the byte and set it as potentially compressable
					{
						unsigned char _myZero = 0;
						if (addToSave(h,buff,1)==-2 || addToSave(h,&_myZero,1)==-2){
							return -2;
						}
						h->isCompress=-1;
						break;
					}
					case 1: // continue the compress
					{
						if (_curByte==h->data[h->curUsed-3]){ // init the compress if it's the same byte
							// convert from big endian
							uint16_t _curCount = (((h->data[h->curUsed-2] & 0x7f) << 8) | h->data[h->curUsed-1]);
							if (_curCount==32767){ // max reached. start a new compress if you want this.
								h->isCompress=0;
								goto switchcompresstype;
							}
							// increment it
							++_curCount;
							// write it again as big endian
							_curCount = htobe16(_curCount);
							h->data[h->curUsed-2] = ((unsigned char*)(&_curCount))[0] | 0x80;
							h->data[h->curUsed-1] = ((unsigned char*)(&_curCount))[1];
							break;
						}else{
							h->isCompress=0; // start a new potential compress
							goto switchcompresstype;
						}
						
					}
				}
				
			}else{ // for a regular packet, just add the byte
				h->isCompress=0;
				if (addToSave(h,buff,1)==-2){
					return -2;
				}
				// write the byte that tells if packet data follows
				unsigned char _nextIsPacketData=h->saveThisContent;
				if (addToSave(h,&_nextIsPacketData,1)==-2){
					return -2;
				}
			}
			h->headerProgress=0;
		}else{
			// save the current packet header byte
			if (addToSave(h,buff,1)==-2){
				return -2;
			}
			h->headerProgress++;
		}
		++buff;
		if (--size!=0){ // let's go again!
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
	struct gpgInfo* _myInfo = handle;
	if (processHeaderBak(&_myInfo->hInfo,buffer,size)==-2){
		errno=EIO;
		return -1;
	}
	_myInfo->curHash = crc32_z(_myInfo->curHash,buffer,size);
	return iomodeWrite(_myInfo->out,_myInfo->iomode,buffer,size);
}
// return n bytes read, 0 on EOF, -1 on error
ssize_t myRead(void *handle, void *buffer, size_t size){
	struct gpgInfo* _myInfo = handle;
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
	h->isCompress=0;
	h->packetType=0;
}
void initInfo(struct gpgInfo* i){
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
signed char getFileSize(FILE* fp, size_t* ret){
	struct stat st;
	if (fstat(fileno(fp),&st)){
		return -2;
	}
	*ret=st.st_size;
	return 0;
}
signed char woarcInitSource(size_t i, struct fileMeta* infoDest, void** srcDest, void* _userData){
	struct fileCallInfo* _passedInfo = _userData;
	if (!(*srcDest=fopen(_passedInfo->fileList[i]->filename,"rb"))){
		perror("woarcInitSource");
		return -2;
	}
	infoDest->len=_passedInfo->fileList[i]->size;
	infoDest->lastModified=0;
	return 0;
}
signed char woarcCloseSource(size_t i, void* _closeThis, void* _userData){
	return fclose(_closeThis)==0 ? 0 : -2;
}
signed char woarcGetFilename(size_t i, char** dest, void* _userData){
	struct fileCallInfo* _info = _userData;
	*dest=_info->fileList[i]->filename+strlen(_info->rootDir);
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
			perror("woarcReadData");
			return -2;
		}else if (feof(src)){
			return -1;
		}
	}
	return 0;
}
signed char woarcGetFileProp(size_t i, uint8_t* _propDest, uint16_t* _propPropDest, void* _userData){
	*_propDest=0;
	*_propPropDest=0;
	return 0;
}
//////////////
struct compressState* allocCompressStateWithCallback(){
	struct compressState* s = allocCompressState();
	struct userCallbacks* c = getCallbacks(s);
	c->initSourceFunc=woarcInitSource;
	c->closeSourceFunc=woarcCloseSource;
	c->getFilenameFunc=woarcGetFilename;
	c->getCommentFunc=woarcGetComment;
	c->getSourceData=woarcReadData;
	c->getPropFunc=woarcGetFileProp;
	return s;
}

//////////////
// may exit(1)
void forceArgEndInSlash(const char* _passedFolder){
	int _cachedLen = strlen(_passedFolder);
	if (!(_cachedLen && _passedFolder[_cachedLen-1]==SEPARATOR)){
		fprintf(stderr,"expected string to end with %c: \"%s\"\n",SEPARATOR,_passedFolder);
		exit(1);
	}
}
// read file into array
char readFileNoBlanks(const char* _file, size_t* _retSize, char*** _retArray){
	*_retArray=NULL;
	*_retSize=0;
	FILE* fp = fopen(_file,"rb");
	if (fp==NULL){
		return 1;
	}
	char _ret=0;
	size_t _curMaxArray=0;
	size_t _curArrayElems=0;
	size_t _lineSize=0;
	char* _lastLine=NULL;
	while (1){
		errno=0;
		if (getline(&_lastLine,&_lineSize,fp)==-1){
			_ret=(errno!=0);
			break;
		}
		if (removeNewline(_lastLine)){ // skip blank lines
			continue;
		}
		if (_curArrayElems>=_curMaxArray){
			_curMaxArray+=10;
			if (!(*_retArray = realloc(*_retArray,sizeof(char*)*_curMaxArray))){
				fprintf(stderr,"readFileNoBlanks alloc failed\n");
				_ret=1;
				goto cleanup;
			}
		}
		(*_retArray)[_curArrayElems++]=strdup(_lastLine);
	}
cleanup:
	free(_lastLine);
	if (fclose(fp)==EOF){
		return 1;
	}
	*_retSize=_curArrayElems;
	return _ret;
}
void freeTinyRead(size_t _arrLength, char** _arr){
	int i;
	for (i=0;i<_arrLength;++i){
		free(_arr[i]);
	}
	free(_arr);
}

// will remove things from _allFileList and put them into _destList
signed char getGoodFileList(size_t _maxSize, struct newFile** _allFileList, size_t _allFileListLen, struct newFile*** _destList, size_t* _destSize){
	struct nList* _myList=NULL;
	struct nList** _adder = initSpeedyAddnList(&_myList);
	*_destSize=0;
	size_t _curSize=_maxSize;
	size_t i;
	for (i=0;i<_allFileListLen;++i){
		if (_allFileList[i] && _allFileList[i]->size<=_curSize){
			_curSize-=_allFileList[i]->size;
			if (!(_adder=speedyAddnList(_adder, _allFileList[i]))){
				return -2;
			}
			++(*_destSize);
			_allFileList[i]=NULL;
		}
	}
	endSpeedyAddnList(_adder);
	if (*_destSize==0){
		fprintf(stderr,"todo - partial file code\n");
		return -2;
	}
	// convert to array for happy fun times
	*_destList = malloc(sizeof(struct newFile*)*(*_destSize));
	i=0;
	ITERATENLIST(_myList,{
			((*_destList)[i++])=_curnList->data;
		});
	freenList(_myList,0);
	return 0;
}
// note that _fromHere and _bigList are both already sorted from biggest to smallest
void shoveBackInBigList(struct newFile** _fromHere, size_t _fromHereSize, struct newFile** _bigList, size_t _maxBigList){
	// take things from _fromHere and shove them back into the null slots of _bigList
	size_t i;
	size_t _fromFilesPut=0;
	size_t _lastFreeSlot;
	for (i=0;i<_maxBigList;++i){
		if (!_bigList[i]){
			if (_bigList[i]->size<_fromHere[_fromFilesPut]->size){ // if the next file we'll put should go before this one
				_bigList[_lastFreeSlot]=_fromHere[_fromFilesPut];
				if (++_fromFilesPut==_fromHereSize){
					return;
				}
			}
		}else{
			_lastFreeSlot=i;
		}
	}
	// if we're here, that means we reached the end and still have files left
	// we have all the smallest files in our array. so just put them in.
	// it's guarentted that the number of files we have left, there are at least that many open slots at the right end of the array
	for (i=_maxBigList-(_fromHereSize-_fromFilesPut);_fromFilesPut!=_fromHereSize;++_fromFilesPut,++i){
		_bigList[i]=_fromHere[_fromFilesPut];
	}
}

#define MYTESTFOLDER "/tmp/testfolder/"
#define TESTLASTSEEN "/tmp/lastseen"
int main(int argc, char** args){
	///////////////////////////////////
	// init get filenames
	///////////////////////////////////
	forceArgEndInSlash(MYTESTFOLDER);
	size_t _newListLen;
	struct newFile** _newFileList;
	getNewFiles(MYTESTFOLDER,/*"/tmp/inc"*/NULL,/*"/tmp/exc"*/NULL,TESTLASTSEEN,&_newListLen,&_newFileList);
	if (_newListLen==0){
		fprintf(stderr,"no new files found\n");
		return 1;
	}
	size_t _newFilesLeft=_newListLen;
	int i;
	for (i=0;i<_newListLen;++i){
		printf("%ld;%d;%s\n",_newFileList[i]->size,_newFileList[i]->type,_newFileList[i]->filename);
	}
	///////////////////////////////////
	// init gpg
	///////////////////////////////////
	fprintf(stderr,"version: %s\n",gpgme_check_version(NULL));
	failIfError(gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP),"openpgp engine check");
	// init pass info
	struct gpgInfo _myInfo;
	initInfo(&_myInfo);
	void* _myHandle=&_myInfo;
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
	///////////////////////////////////
	// partial compress state init
	///////////////////////////////////
	struct compressState* _myCompress = allocCompressStateWithCallback();
	struct fileCallInfo* _compressInfo;
	if (!(_compressInfo=malloc(sizeof(struct fileCallInfo)))){
		fprintf(stderr,"out of memory on alloc compress info\n");
		goto cleanup;
	}
	_compressInfo->rootDir=MYTESTFOLDER;
	getCallbacks(_myCompress)->userData=_compressInfo;
	///////////////////////////////////
	// disc init
	// from here on out, do not use exit(1)
	////////////////////////////////////
	// TODO - disc init

	///////////////////////////////////
	///////////////////////////////////
	///////////////////////////////////
	// LOOP STARTS HERE
	///////////////////////////////////
	///////////////////////////////////
	///////////////////////////////////
	char _lastTestFile[255];
	int __testfilenameNum=0;	
	do{
		// reset state info
		initInfo(&_myInfo);
		//////////////////////////////
		// Open a new disc
		//////////////////////////////
		sprintf(_lastTestFile,"/tmp/arcout%d",__testfilenameNum++);
		_myInfo.iomode=IOMODE_FILE;
		_myInfo.out=fopen(_lastTestFile,"wb");
		
		///////////////////////////////////
		// get filenames. this depends on free space on current disc
		///////////////////////////////////
		size_t _numChosenFiles;
		//1000000000
		if (getGoodFileList(1000000000,_newFileList,_newListLen,&(_compressInfo->fileList),&_numChosenFiles)){
			fprintf(stderr,"getGoodFileList failed\n");
			goto cleanup;
		}
		// realloc the state to the new number of files
		if (initCompressState(_myCompress,_numChosenFiles)==-2){
			fprintf(stderr,"error in initCompressState for %ld new files\n",_numChosenFiles);
			goto cleanup;
		}
		///////////////////////////////////
		// Do the assemble, encrypt, write
		///////////////////////////////////
		_myInfo.assembleState=_myCompress;
		if(gpgme_op_encrypt(_myContext,NULL, GPGME_ENCRYPT_NO_COMPRESS | GPGME_ENCRYPT_SYMMETRIC,_myIn,_myOut)!=GPG_ERR_NO_ERROR){
			fprintf(stderr,"gpgme_op_encrypt error\n");
			goto cleanup;
		}
		// write the rest of the file
		if (iomodePutc(_myInfo.out,_myInfo.iomode,0)==-2){
			perror("post write error 1");
			goto cleanup;
		}
		if (iomodeWriteFail(_myInfo.out,_myInfo.iomode,METADATAMAGIC,strlen(METADATAMAGIC))==-2 ||
			write32(_myInfo.out,_myInfo.iomode,_myInfo.curHash)==-2 ||
			write64(_myInfo.out,_myInfo.iomode,_myInfo.hInfo.curUsed)==-2 ||
			iomodeWriteFail(_myInfo.out,_myInfo.iomode,_myInfo.hInfo.data,_myInfo.hInfo.curUsed)==-2){

			perror("post write error 2");
			goto cleanup;
		}

		///////////////////////////////////
		// finish disc write
		///////////////////////////////////
		if (iomodeClose(_myInfo.out,_myInfo.iomode)){
			perror("finish write - close");
		}
		///////////////////////////////////
		// verify disc
		///////////////////////////////////
		// TODO - eject disc and then put drive back in. this can all be done with code.
		_myInfo.iomode=IOMODE_FILE;
		_myInfo.out=fopen(_lastTestFile,"rb");
		signed char _doUpdateSeen=verifyDisc(_myInfo.out,_myInfo.iomode); // closes the disc IO
		if (_doUpdateSeen!=1){ // if it's bad
			if (forwardUntil("mybodyisready")){
				goto cleanup;
			}
			if (_doUpdateSeen==-1){
				fprintf(stderr,"disc verification failed. ignore? (y/n)\n");
			}else{
				fprintf(stderr,"disc corrupt. ignore? (y/n)\n");
			}
			signed char _doRetry = getYesNoIn();
			if (_doRetry==-1){
				goto cleanup;
			}else if (_doRetry==1){ // yes, do ignore
				fprintf(stderr,"Really ignore the problem? these files will not be rewritten.\n");
				switch(getYesNoIn()){
					case -1:
						goto cleanup;
					case 1:
						_doUpdateSeen=1; // continue as if the disc was correct
						break;
					case 0:
						goto putfilesback;
				}
			}else{
			putfilesback:
				// put the files back into the big array.
				shoveBackInBigList(_compressInfo->fileList,_numChosenFiles,_newFileList,_newListLen);
				_doUpdateSeen=0;
				free(_compressInfo->fileList);
				_compressInfo->fileList=NULL;
			}
		}
		///////////////////////////////////
		// finish up
		///////////////////////////////////
		if (_doUpdateSeen){
			// rewrite the last seen file to reflect that we've written this new stuff
			if (appendToLastSeenList(TESTLASTSEEN,_compressInfo->rootDir,_compressInfo->fileList,_numChosenFiles)){
				goto cleanup;
			}
		}
		// our current list of files is done. free it.
		if (_compressInfo->fileList){
			for (i=0;i<_numChosenFiles;++i){
				free(_compressInfo->fileList[i]->filename);
				free(_compressInfo->fileList[i]);
			}
			free(_compressInfo->fileList);
			_newFilesLeft-=_numChosenFiles;
		}

		if (_newFilesLeft!=0){
			if (forwardUntil("mybodyisready")){
				goto cleanup;
			}
			fprintf(stderr,"burn another disc? (y/n)\n");
			if (getYesNoIn()!=1){
				break;
			}
		}else{
			fprintf(stderr,"HAPPY END\n");
			break;
		}
	}while(1);
	///////////////////////////////////
	///////////////////////////////////
	///////////////////////////////////
	// LOOP ENDS HERE
	///////////////////////////////////
	///////////////////////////////////
	///////////////////////////////////
	

	///////////////////////////////////
	// free nonsense just to supress valgrind errors
	///////////////////////////////////
	free(_newFileList);
	
cleanup:
	gpgme_release(_myContext);
	// TODO - drive cleanup
}
