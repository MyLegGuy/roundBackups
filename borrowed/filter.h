/*
	Copyright (C) 2020  MyLegGuy
	This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
	You should have received a copy of the GNU General Public License along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
// These must all use characters unused by utf8
#define FILTER_WILDCARD 0xFF // Note that star supports matching 0 characters.
//
#define FLAG_FILE 1
#define FLAG_FOLDER 2
#define FLAG_FILEPATH 4 // otherwise it's filename only
//
struct filterEntry{
	char* pattern;
	unsigned char flag;
};

void fixFilter(char* _filter);
struct filterEntry* loadFilter(const char* _filepath, size_t* _retLen);
char filterMatches(const unsigned char* _test, int _testLen, const unsigned char* _filter);
char isFiltered(const char* _passedPath, unsigned char _passedType, int _numFilters, struct filterEntry* _filters);
