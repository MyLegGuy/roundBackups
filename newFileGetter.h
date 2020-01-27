#define NEWFILE_FILE 1
#define NEWFILE_SYM 2
struct newFile{
	char* filename; // malloc
	size_t size;
	char type;
};
// may exit(1)
void getNewFiles(const char* _rootFolder, const char* _includeListFilename, const char* _excludeListFilename, const char* _seenListFilename, size_t* _retLen, struct newFile*** _retList);
