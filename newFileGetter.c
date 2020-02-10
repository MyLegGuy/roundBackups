/*
	Copyright (C) 2020  MyLegGuy
	This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
	You should have received a copy of the GNU General Public License along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#define _XOPEN_SOURCE 500
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <ftw.h>
#include <errno.h>
#include "config.h"
#include "newFileGetter.h"
#include "main.h"
#include "borrowed/bsdnftw.h"
#include "borrowed/goodLinkedList.h"
#include "borrowed/filter.h"
struct checkNewInfo{
	const char* rootDir;
	struct nList** speedyAdder;

	struct filterEntry* includeFilters;
	size_t numIncludes;
	struct filterEntry* excludeFilters;
	size_t numExcludes;

	char** seenList;
	size_t seenListSize;
};
// sorts so that the biggest file is first
int newFileSizeCompare(const void* uncastp1, const void* uncastp2){
	struct newFile* p1 = *(struct newFile**)uncastp1;
	struct newFile* p2 = *(struct newFile**)uncastp2;
	if (p1->size>p2->size){
		return -1;
	}else if (p1->size<p2->size){
		return 1;
	}
	return 0;
}
// may want to replace this with binary search later
char seenListContains(const char* _checkFor, size_t _listSize, char** _list){
	int i;
	for (i=0;i<_listSize;++i){
		if (strcmp(_checkFor,_list[i])==0){
			return 1;
		}
	}
	return 0;
}
int checkSingleIsNew(const char *fpath, const struct stat *sb, int typeflag, struct FTW* ftwbuf, void* _arg){
	struct checkNewInfo* _passedCheck = _arg;
	int _cachedRootStrlen = strlen(_passedCheck->rootDir);
	if (strncmp(fpath,_passedCheck->rootDir,_cachedRootStrlen)!=0){
		fprintf(stderr,"Bad root to path. Path is: %s root is %s\n",fpath,_passedCheck->rootDir);
		return 1;
	}
	// get type flag for filters
	unsigned char _filterTypePass;
	if (typeflag==FTW_F){
		_filterTypePass=FLAG_FILE;
	}else if (typeflag==FTW_D){
		_filterTypePass=FLAG_FOLDER;
	}else{ // For unknown stuff, try to match it with any filters
		_filterTypePass=(FLAG_FILE | FLAG_FOLDER);
	}
	// Process includes and excludes if used
	char _isQuit=0;
	if (_passedCheck->numIncludes!=0){
		if (!isFiltered(fpath,_filterTypePass,_passedCheck->numIncludes,_passedCheck->includeFilters)){
			printf("Not included: %s\n",fpath);
			_isQuit=1;
		}
	}
	if (!_isQuit && _passedCheck->numExcludes!=0){
		if (isFiltered(fpath,_filterTypePass,_passedCheck->numExcludes,_passedCheck->excludeFilters)){
			printf("Excluded: %s\n",fpath);
			_isQuit=1;
		}
	}
	if (_isQuit){
		if (typeflag==FTW_D){
			return 2; // Skip dir contents
		}else{
			return 0; // for unknown things and files - return OK
		}
	}
	// files or symlinks
	if (typeflag==FTW_F || (typeflag==FTW_SL || typeflag==FTW_SLN)){
		if (!seenListContains(&fpath[_cachedRootStrlen],_passedCheck->seenListSize,_passedCheck->seenList)){
			struct newFile* _thisFile = malloc(sizeof(struct newFile));
			_thisFile->filename=strdup(fpath);
			if (!_thisFile->filename){
				perror("checkSingleIsNew filename strdup");
				return 1;
			}
			_thisFile->lastModified=sb->st_mtime;
			if (typeflag==FTW_F){
				_thisFile->type=NEWFILE_FILE;
				_thisFile->size=sb->st_size;
			}else if (typeflag==FTW_SL || typeflag==FTW_SLN){
				_thisFile->type=NEWFILE_SYM;
				_thisFile->symInfo=malloc(sizeof(struct newFileSym));
				if (!_thisFile->symInfo){
					perror("malloc syminfo in checkSingleIsNew");
					return 1;
				}
				// get the symlink dest
				char* _dest;
				if (!(_dest=realpath(_thisFile->filename,NULL))){
					perror("realpath in checkSingleIsNew");
					return 1;
				}
				// if the root is the same, we can store it as a relative symlink
				if (strncmp(_dest,_passedCheck->rootDir,_cachedRootStrlen)==0){
					_thisFile->symInfo->isRelative=1;
					_thisFile->symInfo->symDest=strdup(&_dest[_cachedRootStrlen]);
				}else{ // otherwise store it as an absolute symlink
					fprintf(stderr,"warning: link to file not in backup directory: %s\n",_dest);
					_thisFile->symInfo->isRelative=0;
					_thisFile->symInfo->symDest=strdup(_dest); // duplicate it anyway because the one returned by realpath is massive
				}
				if (!_thisFile->symInfo->symDest){
					perror("checkSingleIsNew strdup");
					return 1;
				}
				free(_dest);
				_thisFile->symInfo->pushedBytes=0;
				_thisFile->size=strlen(_thisFile->symInfo->symDest);
			}else{ // impossible case
				return 1;
			}
			if (!(_passedCheck->speedyAdder=speedyAddnList(_passedCheck->speedyAdder,_thisFile))){
				return 1;
			}
		}
	}else{
		if (typeflag==FTW_DNR){
			fprintf(stderr,"unreadable directory %s\n",fpath);
			return 1;
		}else if (typeflag!=FTW_D){
			fprintf(stderr,"Unknown thing passed.\n%d:%s\n",typeflag,fpath);
			return 1;
		}
	}
	return 0;
}
// read file into array
static char readLastSeenList(const char* _file, size_t* _retSize, char*** _retArray, uint64_t* _retMaxDisc){
	*_retMaxDisc=0;
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
		int _firstChar = fgetc(fp);
		if (_firstChar==EOF){
			_ret=!feof(fp);
			break;
		}
		if (_firstChar==0){ // if the line starts with 0x00 then it's a disc ID line
			uint64_t _disc;
			if (fread(&_disc,1,sizeof(uint64_t),fp)!=sizeof(uint64_t)){
				fprintf(stderr,"read error\n");
				_ret=1;
				break;
			}
			_disc=le64toh(_disc);
			if (_disc>*_retMaxDisc){
				*_retMaxDisc=_disc;
			}
			continue;
		}else{
			if (ungetc(_firstChar,fp)==EOF){
				fprintf(stderr,"ungetc error\n");
				_ret=1;
				break;
			}
		}
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
		if (!((*_retArray)[_curArrayElems++]=strdup(_lastLine))){
			perror("readLastSeenList strdup");
			_ret=1;
			goto cleanup;
		}
	}
cleanup:
	free(_lastLine);
	if (fclose(fp)==EOF){
		return 1;
	}
	*_retSize=_curArrayElems;
	return _ret;
}
// may exit(1)
void getNewFiles(const char* _rootFolder, const char* _includeListFilename, const char* _excludeListFilename, const char* _seenListFilename, size_t* _retLen, struct newFile*** _retList, uint64_t* _maxDiscNum){
	struct nList* _linkedList=NULL;
	struct checkNewInfo _checkInfo;
	if (readLastSeenList(_seenListFilename,&_checkInfo.seenListSize,&_checkInfo.seenList,_maxDiscNum)){
		fprintf(stderr,"error reading %s\n",_seenListFilename);
		exit(1);
	}
	if (_includeListFilename){
		_checkInfo.includeFilters = loadFilter(_includeListFilename,&_checkInfo.numIncludes);
	}else{
		_checkInfo.numIncludes=0;
	}
	if (_excludeListFilename){
		_checkInfo.excludeFilters = loadFilter(_excludeListFilename,&_checkInfo.numExcludes);
	}else{
		_checkInfo.numExcludes=0;
	}
	_checkInfo.rootDir=_rootFolder;
	_checkInfo.speedyAdder = initSpeedyAddnList(&_linkedList);
	if (nftwArg(_rootFolder,checkSingleIsNew, 5, FOLLOWSYMS ? 0 : FTW_PHYS, &_checkInfo)==-1){
		fprintf(stderr,"nftw error\n");
		exit(1);
	}
	endSpeedyAddnList(_checkInfo.speedyAdder);
	freeTinyRead(_checkInfo.seenListSize,_checkInfo.seenList);
	//
	*_retLen=nListLen(_linkedList);
	*_retList = malloc(sizeof(struct newFile*)*(*_retLen));
	size_t i=0;
	ITERATENLIST(_linkedList,{
			((*_retList)[i++])=_curnList->data;
		});
	freenList(_linkedList,0);
	// sort the array by size with biggest first
	qsort(*_retList,*_retLen,sizeof(struct newFile*),newFileSizeCompare);
}
signed char appendToLastSeenList(const char* _lastSeenFilename, const char* _rootDir, struct newFile** _fileInfo, size_t _numFiles, uint64_t _discNum){
	FILE* fp = fopen(_lastSeenFilename,"ab");
	if (!fp){
		perror("appendToLastSeenList a");
		return -2;
	}
	// write the disc number
	if (fputc(0,fp)==EOF){
		goto err;
	}
	_discNum=htole64(_discNum);
	if (fwrite(&_discNum,1,sizeof(uint64_t),fp)!=sizeof(uint64_t)){
		goto err;
	}
	//
	size_t _cachedRootLen = strlen(_rootDir);
	size_t i;
	for (i=0;i<_numFiles;++i){
		const char* _writeStr = _fileInfo[i]->filename+_cachedRootLen;
		if (fwrite(_writeStr,1,strlen(_writeStr),fp)!=strlen(_writeStr)){
			goto err;
		}
		if (fputc('\n',fp)==EOF){
			goto err;
		}
	}
	if (fclose(fp)==EOF){
		perror("appendToLastSeenList b");
		return -2;
	}
	return 0;
err:
	perror("appendToLastSeenList c");
	fclose(fp);
	return -2;
}
// does not free the pointer itself though
void freeNewFile(struct newFile* f){
	free(f->filename);
	if (f->type==NEWFILE_SYM){
		free(f->symInfo->symDest);
		free(f->symInfo);
	}
}
