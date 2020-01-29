#define SEPARATOR '/'
#ifndef linux
	#warning forward slash is used for directory separator
#endif
char removeNewline(char* _toRemove);
void freeTinyRead(size_t _arrLength, char** _arr);
char readFileNoBlanks(const char* _file, size_t* _retSize, char*** _retArray);
