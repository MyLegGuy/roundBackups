/*
	Copyright (C) 2020  MyLegGuy
	This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
	You should have received a copy of the GNU General Public License along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#define METADATAMAGIC "ROUNDEND"
#define ROUNDVERSIONNUM 1

// AES will always round to multiple of this
#define COMPRESSIONBLOCKSIZE 16

// the maximum number of session key packets that can be copied for the sessionKeyPacketBackup
#define MAXSESSIONKEYPACKETSCOPIED 2
// i've seen lengths of 268 and also around 240
// so just be safe
#define ASSUMEDSESSIONKEYPACKETLEN 500
//
#define ROUNDMETADATABASEOVERHEAD (										\
		1+																\
		strlen(METADATAMAGIC)+											\
		1+																\
		8+																\
		4+																\
		8+																\
		ASSUMEDSESSIONKEYPACKETLEN*MAXSESSIONKEYPACKETSCOPIED+			\
		COMPRESSIONBLOCKSIZE+											\
		271+2+1+2+1+1+4+1+20 /* some gpg packet data. 527 is for 4096 key */ \
		)

// on my system, the partial continue packets have this much data. therefor i assume this will be true on all systems.
#define PARTIALCONTINUE 8192
// how much extra metadata is there if we add this many bytes to the pgp file?
// this can be found by the number of partial length continue packet headers there are
// also we double it for some reason
#define GPGEXTRAMETADATAOVERHEAD(byteCount) (((byteCount)/PARTIALCONTINUE+1)*2)
