/*
	Copyright (C) 2020  MyLegGuy
	This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
	You should have received a copy of the GNU General Public License along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
// TODO - check permissions at the start for all passed files
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <gpgme.h>
#include <zlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <endian.h>

#include "config.h"
#include "verify.h"
#include "iomode.h"
#include "userIn.h"
#include <woarcFormatInfo.h>
#include <woarcassemble.h>
#include "borrowed/goodLinkedList.h"
#include "borrowed/filter.h"
#include "roundFormat.h"
#include "main.h"
#include "newFileGetter.h"

#define TOPBACKUPBYTES 100
struct headerBak{
	unsigned char* data;
	uint64_t curUsed;
	uint64_t size;

	unsigned char numBuff[4]; // for 5 byte numbers
	signed char headerProgress; // -1 if done. need to use like that because curLeft can be 0 after valid packet
	signed char packetType; // if 0 then regular new packet. if -1 then partial length new packet. if > 0 then it's an old packet and the number is the number of length bytes.
	uint64_t curLeft;
	char saveThisContent;
	int numPacketsSoFar;
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
// returns 1 on fail
char runProgram(char* const _args[]){
	pid_t _newProcess = fork();
	if (_newProcess==-1){
		return 1;
	}else if (_newProcess==0){ // 0 is returned to the new process
		// First arg is the path of the file again
		execv(_args[0],_args); // This will do this program and then end the child process
		exit(1); // This means execv failed
	}
	int _retStatus;
	waitpid(_newProcess,&_retStatus,0);
	return !WIFEXITED(_retStatus);
}
char ejectRealDrive(){
	char* const _args[2]={EJECTPATH,NULL};
	return runProgram(_args);
}
char closeRealDrive(){
	char* const _args[3]={EJECTPATH,"-t",NULL};
	return runProgram(_args);
}
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
				h->saveThisContent=(_gottenTag==1 || _gottenTag==3);
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
			if (h->saveThisContent && addToSave(h,buff,1)==-2){
				return -2;
			}
			h->headerProgress=0;
		}else{
			// save the current packet header byte
			if (h->saveThisContent && addToSave(h,buff,1)==-2){
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
			h->numPacketsSoFar++;
			if (h->numPacketsSoFar<MAXSESSIONKEYPACKETSCOPIED){
				goto top;
			}
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
	if (_myInfo->hInfo.numPacketsSoFar<MAXSESSIONKEYPACKETSCOPIED && processHeaderBak(&_myInfo->hInfo,buffer,size)==-2){
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
	h->packetType=0;
	h->numPacketsSoFar=0;
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
static int _lastOutputFileNum=0;
static int _lastTestFileRootLen=0;
static char _lastTestFile[PATH_MAX]; // yes i know this is unsafe. no i dont care
// switch the current disc or maybe the current file
signed char iomodeSwitch(void* _out, char _type, char** _filename, char _userInEnabled){
	switch(_type){
		case IOMODE_DISC:
			if (_userInEnabled){
				// eject the tray. use whatever the user puts in.
				forwardUntil("mynewdiscisready");
			}
			return 0;
		case IOMODE_FILE:
			if (!_lastTestFile[0]){ // first run
				_lastTestFileRootLen=strlen(*_filename);
				strcpy(_lastTestFile,*_filename);
				free(*_filename);
				*_filename=_lastTestFile;
			}
			do{
				sprintf(&_lastTestFile[_lastTestFileRootLen],"%d",_lastOutputFileNum++);
			}while (access(_lastTestFile,F_OK)==0);
			return 0;
		case IOMODE_FAKE:
			return 0;
	}
	return -2;
}
// returns -2 if failed
// for discs, this shoves in the drive handle
signed char iomodeOpen(void** _outOut, char _requestedType, char _isWrite, char* _filename){
	switch(_requestedType){
		case IOMODE_DISC:
		{
			struct iomodeDisc* d=*_outOut;
			d->driveList=openDrive(_filename);
			d->isWrite = !!_isWrite;
			if (_isWrite){
				int _formatRet = libburner_formatBD(getDrive(d->driveList));
				if (_formatRet==-1){
					printf("Note - not a BD\n");
				}else if (_formatRet!=1){
					fprintf(stderr,"BD format err!\n");
					return -2;
				}
				d->state = malloc(sizeof(struct burnState));
			}else{
				int* _dataTrackPos;
				int _numTracks;
				if (getDataTrackPositions(getDrive(d->driveList), &_dataTrackPos, &_numTracks)){
					fprintf(stderr,"getDataTrackPos failed\n");
					return -2;
				}
				if (_numTracks<=0){
					fprintf(stderr,"no tracks on disc\n");
					return -2;
				}
				struct discReadState* dr = malloc(sizeof(struct discReadState));
				if (!dr){
					fprintf(stderr,"state malloc failed\n");
					return -2;
				}
				d->state = dr;
				dr->drive=getDrive(d->driveList);
				dr->curSector = _dataTrackPos[(_numTracks-1)*2]; // last sector start
				dr->maxSector = _dataTrackPos[(_numTracks-1)*2+1]; // last sector end
				dr->buffPushed=SECTORSIZE;
				dr->buffSize=0;
				dr->buffPushed=0;
				free(_dataTrackPos);
			}
			return (d->driveList==NULL || d->state==NULL) ? -2 : 0;
		}
		case IOMODE_FILE:
			*_outOut = fopen(_filename,_isWrite ? "wb" : "rb");
			return (*_outOut==NULL) ? -2 : 0;
		case IOMODE_FAKE:
			return 0;
	}
	return -2;
}
//////////////
signed char woarcCloseSource(size_t i, void* _closeThis, void* _userData){
	if (((struct fileCallInfo*)_userData)->fileList[i]->type==NEWFILE_FILE){
		return fclose(_closeThis)==0 ? 0 : -2;
	}
	return 0;
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
// shove the symlink dest into the buffer
signed char woarcReadButItsNewFileSym(void* src, char* dest, size_t requested, size_t* actual){
	struct newFileSym* s = src;
	if (s->pushedBytes+requested<strlen(s->symDest)){ // if we're requesting less than is left
		memcpy(dest,s->symDest+s->pushedBytes,requested);
		s->pushedBytes+=requested;
		*actual=requested;
		return 0;
	}else{ // if we're requested more than we have, fill up as much as possible
		*actual=strlen(s->symDest)-s->pushedBytes;
		memcpy(dest,s->symDest+s->pushedBytes,*actual);
		return -1; // eof
	}
}
signed char woarcGetFileProp(size_t i, uint8_t* _propDest, uint16_t* _propPropDest, void* _userData){
	struct fileCallInfo* _passedInfo = _userData;
	if (_passedInfo->fileList[i]->type==NEWFILE_FILE){
		*_propDest=FILEPROP_NORMAL;
		*_propPropDest=0;
	}else if (_passedInfo->fileList[i]->type==NEWFILE_SYM){
		*_propDest=FILEPROP_LINK;
		*_propPropDest=!(_passedInfo->fileList[i]->symInfo->isRelative);
	}
	return 0;
}
signed char woarcInitSource(size_t i, struct fileMeta* infoDest, void** srcDest, struct userCallbacks* c, void* _userData){
	struct fileCallInfo* _passedInfo = _userData;
	printf("open %s\n",_passedInfo->fileList[i]->filename);
	if (_passedInfo->fileList[i]->type==NEWFILE_FILE){
		if (!(*srcDest=fopen(_passedInfo->fileList[i]->filename,"rb"))){
			perror("woarcInitSource");
			return -2;
		}
		c->getSourceData=woarcReadData;
	}else if (_passedInfo->fileList[i]->type==NEWFILE_SYM){
		*srcDest=_passedInfo->fileList[i]->symInfo;
		_passedInfo->fileList[i]->symInfo->pushedBytes=0;
		c->getSourceData=woarcReadButItsNewFileSym;
	}
	infoDest->len=_passedInfo->fileList[i]->size;
	infoDest->lastModified=_passedInfo->fileList[i]->lastModified;
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
void freeTinyRead(size_t _arrLength, char** _arr){
	int i;
	for (i=0;i<_arrLength;++i){
		free(_arr[i]);
	}
	free(_arr);
}

// will remove things from _allFileList and put them into _destList
signed char getGoodFileList(size_t _maxSize, struct newFile** _allFileList, const char* _rootDir, size_t _allFileListLen, struct newFile*** _destList, size_t* _destSize){
	struct nList* _myList=NULL;
	struct nList** _adder = initSpeedyAddnList(&_myList);
	*_destSize=0;
	size_t _curSize=_maxSize;
	int _cachedRootStrlen = strlen(_rootDir);
	size_t i;
	for (i=0;i<_allFileListLen;++i){
		if (_allFileList[i] && _allFileList[i]->size<=_curSize){ // fast check
			size_t _actualUsedSize = _allFileList[i]->size+WOARCSINGLEFILEBASEOVERHEAD+WOARCFILENAMEMETADATASPACE(strlen(_allFileList[i]->filename)-_cachedRootStrlen); // full check
			_actualUsedSize+=GPGEXTRAMETADATAOVERHEAD(_actualUsedSize);
			if (_actualUsedSize<=_curSize){
				_curSize-=_actualUsedSize;
				if (!(_adder=speedyAddnList(_adder, _allFileList[i]))){
					return -2;
				}
				++(*_destSize);
				_allFileList[i]=NULL;
			}
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
		if (_bigList[i]){
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
char* usedOrNull(char* _passed){
	return strcmp(_passed,"-")==0 ? NULL : _passed;
}
int main(int argc, char** args){
	char _userInEnabled=1; // 0 if no input from user allowed.
	if (argc==2){
		printf("%d\n",verifyDiscFile(args[1]));
		return 0;
	}
	if (argc!=9){
		if (argc==10 && strcmp(args[9],"--nostdin")==0){
			_userInEnabled=0;
		}else{
			printf("%s <file/disc/fake> <out filepath> <root dir> <seen log filename> <discSetNum> <key fingerprint> <inc list file> <exc list file> [--nostdin]\n",args[0]);
			return 1;
		}
	}
	char _userChosenMode;
	if (strcmp(args[1],"file")==0){
		_userChosenMode=IOMODE_FILE;
	}else if (strcmp(args[1],"disc")==0){
		_userChosenMode=IOMODE_DISC;
	}else if (strcmp(args[1],"fake")==0){
		_userChosenMode=IOMODE_FAKE;
	}else{
		return 1;
	}
	char* _curFilename=strdup(args[2]);
	if (!_curFilename){
		return 1;
	}
	char* _chosenRootDir=args[3];
	char* _lastSeenFilename=args[4];
	char* _userChosenFingerprint=usedOrNull(args[6]);
	uint16_t _setId;
	// parse the set ID
	{
		if (args[5][0]=='.'){
			fprintf(stderr,"decimals are friends, not numbers.\n");
			return 1;
		}
		if (args[5][0]<'0' || args[5][0]>'9'){
			fprintf(stderr,"disc set id must be a number\n");
			return 1;
		}
		int _potentialValue = atoi(args[5]);
		if (_potentialValue<0 || _potentialValue>65535){
			fprintf(stderr,"bad disc set id. must fit in two bytes.\n");
			return 1;
		}
		_setId=_potentialValue;
	}
	///////////////////////////////////
	// init get filenames
	///////////////////////////////////
	forceArgEndInSlash(_chosenRootDir);
	size_t _newListLen;
	struct newFile** _newFileList;
	uint64_t _curDiscNum;
	getNewFiles(_chosenRootDir,usedOrNull(args[7]),usedOrNull(args[8]),_lastSeenFilename,&_newListLen,&_newFileList,&_curDiscNum);
	if (_newListLen==0){
		fprintf(stderr,"no new files found\n");
		return 1;
	}
	size_t _newFilesLeft=_newListLen;
	printf("There are %ld new files.\n",_newFilesLeft);
	///////////////////////////////////
	// init gpg
	///////////////////////////////////
	printf("version: %s\n",gpgme_check_version(NULL));
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
	// get key
	///////////////////////////////////
	gpgme_key_t keys[2]={NULL,NULL}; // must end in null
	if (_userChosenFingerprint){
		if (gpgme_get_key(_myContext,_userChosenFingerprint,&(keys[0]),0)!=GPG_ERR_NO_ERROR || keys[0]==NULL){
			fprintf(stderr,"key with fingerprint \"%s\" not found\n",_userChosenFingerprint);
			exit(1);
		}
	}
	///////////////////////////////////
	// partial compress state init
	///////////////////////////////////
	struct compressState* _myCompress = allocCompressStateWithCallback();
	struct fileCallInfo* _compressInfo;
	if (!(_compressInfo=malloc(sizeof(struct fileCallInfo)))){
		fprintf(stderr,"out of memory on alloc compress info\n");
		goto cleanup;
	}
	_compressInfo->rootDir=_chosenRootDir;
	getCallbacks(_myCompress)->userData=_compressInfo;
	///////////////////////////////////
	// disc init
	// from here on out, do not use exit(1)
	////////////////////////////////////
	switch(_userChosenMode){
		case IOMODE_DISC:
			if (initDiscLib()){
				fprintf(stderr,"drive init and grabbing failed\n");
				goto cleanup;
			}
			break;
		case IOMODE_FILE:
			break;
	}
	//////////////////////////////////
	// more once only code
	/////////////////////////////////
	_myInfo.iomode=_userChosenMode;
	if (iomodeInit(&_myInfo.out,_myInfo.iomode)){
		goto cleanup;
	}
	///////////////////////////////////
	// LOOP STARTS HERE
	///////////////////////////////////
	do{
		// reset state info
		initInfo(&_myInfo);
		//////////////////////////////
		// Open a new disc
		//////////////////////////////
		++_curDiscNum;
		printf("please insert your disc. it will be Disc %ld\n",_curDiscNum);
		if (iomodeSwitch(_myInfo.out,_myInfo.iomode,&_curFilename,_userInEnabled)){
			fprintf(stderr,"iomodeSwitch failed\n");
			goto cleanup;
		}
		if (iomodeOpen(&_myInfo.out,_myInfo.iomode,1,_curFilename)){
			fprintf(stderr,"iomodeOpen failed\n");
			goto cleanup;
		}
		char _didFail=0; // if this is 1 then exit after drive release
		size_t _freeDiscSpace;
		if (iomodeGetFree(_myInfo.out,_myInfo.iomode,&_freeDiscSpace)){
			fprintf(stderr,"iomodeGetFree failed\n");
			{_didFail=1; goto cleanReleaseFail;}
		}
		printf("disc has %ld bytes free\n",_freeDiscSpace);
		if (_freeDiscSpace<MINDISCSPACE){
			if (_userInEnabled){
				forwardUntil("mybodyisready");
				fprintf(stderr,"disc is small.\n");
				printf("this disc free space (%ld) is below the minimum recommended size. continue? (y/n)\n",_freeDiscSpace);
			}
			if (!_userInEnabled || getYesNoIn()!=1){
				{_didFail=1; goto cleanReleaseFail;}
			}
		}
		// account for the base space used by worriedArchive, pgp, and roundBackups
		{
			size_t _minReservedMetadataSpace=ROUNDMETADATABASEOVERHEAD+WOARCBASEMETADATASPACE+COMFYMETADATARESERVED;
			if (_freeDiscSpace<_minReservedMetadataSpace){
				fprintf(stderr,"disc free space (%ld) is less than the minimum space required for metadata (%ld)\n",_freeDiscSpace,_minReservedMetadataSpace);
				{_didFail=1; goto cleanReleaseFail;}
			}
			// account for that extra metadata space in the variable itself
			_freeDiscSpace-=_minReservedMetadataSpace;
		}
		///////////////////////////////////
		// get filenames. this depends on free space on current disc
		///////////////////////////////////
		size_t _numChosenFiles;
		if (getGoodFileList(_freeDiscSpace,_newFileList,_compressInfo->rootDir,_newListLen,&(_compressInfo->fileList),&_numChosenFiles)){
			fprintf(stderr,"getGoodFileList failed\n");
			{_didFail=1; goto cleanReleaseFail;}
		}
		// realloc the state to the new number of files
		if (initCompressState(_myCompress,_numChosenFiles)==-2){
			fprintf(stderr,"error in initCompressState for %ld new files\n",_numChosenFiles);
			{_didFail=1; goto cleanReleaseFail;}
		}
		///////////////////////////////////
		// Do the assemble, encrypt, write
		///////////////////////////////////
		char _leftAppendable = _newFilesLeft==_numChosenFiles || ALWAYSLEAVEAPPENDABLE;
		if (iomodePrepareWrite(_myInfo.out, _myInfo.iomode, _leftAppendable)){
			fprintf(stderr,"iomodePrepareWrite failed\n");
			{_didFail=1; goto cleanReleaseFail;}
		}
		_myInfo.assembleState=_myCompress;
		gpgme_encrypt_flags_t _flagList=GPGME_ENCRYPT_NO_COMPRESS;
		if (!_userChosenFingerprint){
			_flagList|=GPGME_ENCRYPT_SYMMETRIC;
		}
		if(gpgme_op_encrypt(_myContext,_userChosenFingerprint ? keys : NULL,_flagList,_myIn,_myOut)!=GPG_ERR_NO_ERROR){
			fprintf(stderr,"gpgme_op_encrypt error\n");
			{_didFail=1; goto cleanReleaseFail;}
		}
		// write the rest of the file
		// write invalid packet tag id (0) to mark end of pgp
		if (iomodePutc(_myInfo.out,_myInfo.iomode,0)==-2){
			fprintf(stderr,"post write error 1\n");
			{_didFail=1; goto cleanReleaseFail;}
		}
		// write metadata stuff
		if (iomodeWriteFail(_myInfo.out,_myInfo.iomode,METADATAMAGIC,strlen(METADATAMAGIC))==-2 || // magic
			iomodePutc(_myInfo.out,_myInfo.iomode,ROUNDVERSIONNUM)==-2 || // version
			write16(_myInfo.out,_myInfo.iomode,_setId)==-2 || // set id
			write64(_myInfo.out,_myInfo.iomode,_curDiscNum)==-2 || // disc number
			write32(_myInfo.out,_myInfo.iomode,_myInfo.curHash)==-2 || // hash
			write64(_myInfo.out,_myInfo.iomode,_myInfo.hInfo.curUsed)==-2 || // packet header backup
			iomodeWriteFail(_myInfo.out,_myInfo.iomode,_myInfo.hInfo.data,_myInfo.hInfo.curUsed)==-2){

			fprintf(stderr,"post write error 2\n");
			{_didFail=1; goto cleanReleaseFail;}
		}

		///////////////////////////////////
		// finish disc write
		///////////////////////////////////
	cleanReleaseFail:
		if (iomodeClose(_myInfo.out,_myInfo.iomode)){
			fprintf(stderr,"error: finish write - close\n");
			goto cleanup;
		}
		if (_didFail){
			fprintf(stderr,"failure\n");
			goto cleanup;
		}
		///////////////////////////////////
		// verify disc
		///////////////////////////////////
		// eject disc to flush cache
		if (_myInfo.iomode==IOMODE_DISC){
			if (_userInEnabled || DISCEJECTANDRETRACTWORKS){
				if (ejectRealDrive()){
					printf("please open and close the drive\n");
					forwardUntil("ididasyouasked");
				}else if (closeRealDrive()){
					printf("Please close your drive\n");
					forwardUntil("ididasyouasked");
				}
			}
		}
		// reopen disc
		if (iomodeOpen(&_myInfo.out,_myInfo.iomode,0,_curFilename)){
			fprintf(stderr,"iomodeOpen failed for verification\n");
			goto cleanup;
		}
		// print how much free space is left
		if (_leftAppendable){
			size_t _newFreeSpace;
			if (iomodeGetFree(_myInfo.out,_myInfo.iomode,&_newFreeSpace)){
				fprintf(stderr,"failed to get amount of free space on disc\n");
			}else{
				printf("there are %ld (%ld mb) free bytes left\n",_newFreeSpace,_newFreeSpace/1000/1000);
			}
		}
		// verify
		printf("verifying...\n");
		signed char _doUpdateSeen=verifyDisc(_myInfo.out,_myInfo.iomode);
		if (iomodeClose(_myInfo.out,_myInfo.iomode)==-2){
			fprintf(stderr,"iomode close\n");
			goto cleanup;
		}
		if (_doUpdateSeen!=1){ // if it's bad
			signed char _doIgnore;
			if (_userInEnabled){
				if (forwardUntil("mybodyisready")){
					goto cleanup;
				}
				if (_doUpdateSeen==-1){
					fprintf(stderr,"disc verification failed\n");
					printf("disc verification failed. ignore? (y/n)\n");
				}else{
					fprintf(stderr,"disc corrupt.\n");
					printf("disc corrupt. ignore? (y/n)\n");
				}
				_doIgnore = getYesNoIn();
			}else{
				fprintf(stderr,"verification result: %s",_doUpdateSeen==0 ? "corrupt" : "failed");
				_doIgnore=-1;
			}
			if (_doIgnore==-1){
				goto cleanup;
			}else if (_doIgnore==1){ // yes, do ignore
				printf("Really ignore the problem? these files will not be rewritten.\n");
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
		}else{
			printf("disc is good\n");
		}
		///////////////////////////////////
		// finish up
		///////////////////////////////////
		printf("this is the end of Disc %ld\n",_curDiscNum);
		if (_doUpdateSeen){
			// rewrite the last seen file to reflect that we've written this new stuff
			if (appendToLastSeenList(_lastSeenFilename,_compressInfo->rootDir,_compressInfo->fileList,_numChosenFiles,_curDiscNum)){
				goto cleanup;
			}
		}
		// our current list of files is done. free it.
		if (_compressInfo->fileList){
			size_t i;
			for (i=0;i<_numChosenFiles;++i){
				freeNewFile(_compressInfo->fileList[i]);
				free(_compressInfo->fileList[i]);
			}
			free(_compressInfo->fileList);
			_newFilesLeft-=_numChosenFiles;
		}

		printf("There are %ld new files left\n",_newFilesLeft);
		if (_newFilesLeft!=0 && _userInEnabled){
			if (forwardUntil("mybodyisready")){
				goto cleanup;
			}
			printf("burn another disc? (y/n)\n");
			if (getYesNoIn()!=1){
				break;
			}
		}else{
			printf("HAPPY END\n");
			break;
		}
	}while(1);
	///////////////////////////////////
	// LOOP ENDS HERE
	///////////////////////////////////

	///////////////////////////////////
	// free nonsense just to supress valgrind errors
	///////////////////////////////////
	free(_newFileList);
cleanup:
	gpgme_release(_myContext);
	if (_userChosenMode==IOMODE_DISC){
		deinitDiscLib();
	}
}
