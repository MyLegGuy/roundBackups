#define SEPARATOR '/'
#ifndef linux
	#warning forward slash is used for directory separator
#endif
void removeNewline(char* _toRemove);
void freeTinyRead(size_t _arrLength, char** _arr);
char tinyReadFile(const char* _file, size_t* _retSize, char*** _retArray);
