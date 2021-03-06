/*
	Copyright (C) 2020  MyLegGuy
	This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
	You should have received a copy of the GNU General Public License along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
char* evilGetLine(FILE* fp){
	char* _curLine=NULL;
	size_t _curSize=0;
	if (getline(&_curLine,&_curSize,fp)==-1){
		return NULL;
	}
	removeNewline(_curLine);
	return _curLine;
}
// returns 1 on fail
char forwardUntil(const char* _required){
	char _ret;
	char* _curLine=NULL;
	size_t _curSize=0;
	while(1){
		printf("\aPlease input \"%s\"\n",_required);
		if (getline(&_curLine,&_curSize,stdin)==-1 && !feof(stdin)){
			_ret=1;
			break;
		}
		removeNewline(_curLine);
		if (strcmp(_curLine,_required)==0){
			_ret=0;
			break;
		}
	}
	free(_curLine);
	return _ret;
}
// 0 = no
// 1 = yes
// -1 = error
signed char getYesNoIn(){
	while(1){
		char* _lastIn = evilGetLine(stdin);
		if (!_lastIn){
			return -1;
		}
		if (strlen(_lastIn)==1){
			if (_lastIn[0]=='y'){
				free(_lastIn);
				return 1;
			}else if (_lastIn[0]=='n'){
				free(_lastIn);
				return 0;
			}
		}
		free(_lastIn);
	}
}
