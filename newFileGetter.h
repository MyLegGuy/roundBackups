#define NEWFILE_FILE 1
#define NEWFILE_SYM 2
struct newFile{
	char* filename; // malloc
	size_t size;
	char type;
};
// may exit(1)
void getNewFiles(const char* _rootFolder, const char* _includeListFilename, const char* _excludeListFilename, const char* _seenListFilename, size_t* _retLen, struct newFile*** _retList, uint64_t* _maxDiscNum);
// returns -2 on error
signed char appendToLastSeenList(const char* _lastSeenFilename, const char* _rootDir, struct newFile** _fileInfo, size_t _numFiles, uint64_t _discNum);
