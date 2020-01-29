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
#include <stdlib.h>
#include <ftw.h>
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
			_thisFile->size=sb->st_size;
			if (!(_thisFile->type=((typeflag==FTW_F) ? NEWFILE_FILE : ((typeflag==FTW_SL || typeflag==FTW_SLN) ? NEWFILE_SYM : 0)))){
				fprintf(stderr,"i must've registered a new type of valid file but forgot to change this line.\n");
			}
			if (!(_passedCheck->speedyAdder=speedyAddnList(_passedCheck->speedyAdder,_thisFile))){
				return 1;
			}
			printf("Added %s\n",fpath);
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
// may exit(1)
void getNewFiles(const char* _rootFolder, const char* _includeListFilename, const char* _excludeListFilename, const char* _seenListFilename, size_t* _retLen, struct newFile*** _retList){
	struct nList* _linkedList=NULL;
	struct checkNewInfo _checkInfo;
	if (readFileNoBlanks(_seenListFilename,&_checkInfo.seenListSize,&_checkInfo.seenList)){
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
signed char appendToLastSeenList(const char* _lastSeenFilename, const char* _rootDir, struct newFile** _fileInfo, size_t _numFiles){
	FILE* fp = fopen(_lastSeenFilename,"ab");
	if (!fp){
		perror("appendToLastSeenList");
		return -2;
	}
	size_t _cachedRootLen = strlen(_rootDir);
	size_t i;
	for (i=0;i<_numFiles;++i){
		if (fputc('\n',fp)==EOF){
			goto err;
		}
		const char* _writeStr = _fileInfo[i]->filename+_cachedRootLen;
		if (fwrite(_writeStr,1,strlen(_writeStr),fp)!=strlen(_writeStr)){
			goto err;
		}
	}
	if (fclose(fp)==EOF){
		perror("appendToLastSeenList");
		return -2;
	}
	return 0;
err:
	perror("appendToLastSeenList");
	fclose(fp);
	return -2;
}
