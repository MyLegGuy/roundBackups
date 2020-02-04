#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include <stdint.h>
#include "roundFormat.h"
#include "config.h"
#include "iomode.h"
// returns -2 on invalid data
// returns -1 on IO error
// returns 0 on worked
// returns 1 if this is the end of the pgp woarc file (0 read as tag byte)
signed char lowReadPacketHeader(void* _out, char _type, signed char* _packetType, size_t* _destSize, uLong* _curHash){
	unsigned char _numBuff[4];
	int _curByte;
	// tag byte
	if (*_packetType!=-1){ // for partial length headers, skip the tag byte.
		if ((_curByte=iomodeGetc(_out,_type))==EOF){
			return -1;
		}else if (_curByte==0){ // marks the end of the pgp file. this byte is not hashed.
			return 1;
		}
		// put the byte into the buffer so that it can be hashed
		_numBuff[0]=_curByte;
		*_curHash=crc32_z(*_curHash,_numBuff,1);
		//
		if (!(_curByte & 64)){ // old packet format
			*_packetType=1;
			char _numBytes = (((unsigned char)(_curByte<<6))>>6)+1;
			if (_numBytes==4){ // (actually 3, but we added 1.)
				fprintf(stderr,"indeterminate packet length in old packet format unsupported. why is this being used? is this even a packet header?\n");
				return -2;
			}else if (_numBytes==3){
				_numBytes=4;
			}
			*_destSize=0;
			int i;
			for (i=0;i<_numBytes;++i){
				if ((_curByte=iomodeGetc(_out,_type))==EOF){
					return -1;
				}
				unsigned char _tempByte=_curByte;
				*_curHash=crc32_z(*_curHash,&_tempByte,1);
				*_destSize|=(_tempByte<<((_numBytes-1-i)*8));
			}
			return 0;
		}else{ // new packet format
			*_packetType=0;
		}
	}else{
		*_packetType=0;
	}
	// first byte
	if ((_curByte=iomodeGetc(_out,_type))==EOF){
		return -1;
	}
	_numBuff[0]=_curByte;
	*_curHash=crc32_z(*_curHash,_numBuff,1);
	if (*_packetType==0){ // new packet type
		if (_curByte<=191){ // One-Octet Lengths
			*_destSize=_curByte;
			return 0;
		}else if (_curByte>=224 && _curByte<255){ // partial packet
			*_packetType=-1;
			*_destSize=1<<(_numBuff[0] & 0x1F);
			return 0;
		}
	}
	// second byte
	if ((_curByte=iomodeGetc(_out,_type))==EOF){
		return -1;
	}
	_numBuff[1]=_curByte;
	*_curHash=crc32_z(*_curHash,_numBuff+1,1);
	if (*_packetType==0){ // new packet type
		if (_numBuff[0]>=192 && _numBuff[0]<=223){ // Two-Octet Lengths
			*_destSize=((_numBuff[0]-192)<<8)+192+_numBuff[1];
			return 0;
		}
	}
	// third byte
	if ((_curByte=iomodeGetc(_out,_type))==EOF){
		return -1;
	}
	_numBuff[2]=_curByte;
	// fourth byte
	if ((_curByte=iomodeGetc(_out,_type))==EOF){
		return -1;
	}
	_numBuff[3]=_curByte;
	*_curHash=crc32_z(*_curHash,_numBuff+2,2);
	// fifth byte
	if ((_curByte=iomodeGetc(_out,_type))==EOF){
		return -1;
	}
	unsigned char _fifthByte=_curByte;
	*_curHash=crc32_z(*_curHash,&_fifthByte,1);
	if (_numBuff[0]!=255){
		fprintf(stderr,"first byte isn't 255. is %d. bad five-octet length!\n",_numBuff[0]);
		return -2;
	}
	*_destSize=(_numBuff[1] << 24) | (_numBuff[2] << 16) | (_numBuff[3] << 8)  | _fifthByte;
	return 0;
}
// returns 1 if disc is good
// returns 0 if disc is bad
// returns -1 if failed
signed char verifyDisc(void* _out, char _type){
	char buff[MAXVERIFYHASHBUFF];
	signed char _ret;
	uLong _curHash = crc32(0L, Z_NULL, 0);
	size_t _packetSize;
	signed char _packetType=0; // if 0 then regular new packet. if -1 then partial length new packet. if > 0 then it's an old packet and the number is the number of length bytes.
	while(1){
		switch(lowReadPacketHeader(_out,_type,&_packetType,&_packetSize,&_curHash)){
			case -2:
				fprintf(stderr,"actual data content error\n");
			case -1:
				goto earlyend;
			case 0:
				printf("reading %ld\n",_packetSize);
				// read the rest and hash
				while(1){
					size_t _numRead;
					if (_packetSize<MAXVERIFYHASHBUFF){
						_numRead=_packetSize;
					}else{
						_numRead=MAXVERIFYHASHBUFF;
					}
					if (iomodeRead(_out,_type,buff,_numRead)!=_numRead){
						fprintf(stderr,"verifydisc packet content read error\n");
						goto earlyend;
					}
					_curHash = crc32_z(_curHash,buff,_numRead);
					if ((_packetSize-=_numRead)==0){
						break;
					}
				}
				break;
			case 1: // read the hash and make sure they're the same
			{
				// check for METADATAMAGIC
				int _magicLen=strlen(METADATAMAGIC);
				char _readMagic[_magicLen];
				if (iomodeRead(_out,_type,_readMagic,_magicLen)!=_magicLen){
					goto earlyend;
				}
				if (memcmp(_readMagic,METADATAMAGIC,_magicLen)!=0){
					fprintf(stderr,"%s magic corrupt\n",METADATAMAGIC);
					goto earlyend;
				}
				// read the hash
				uint32_t _readHash;
				if (read32(_out,_type,&_readHash)){
					goto earlyend;
				}
				_ret=(_readHash==((uint32_t)_curHash));
				goto cleanup;
			}
				break;
		}
	}
	//
earlyend:
	_ret=-1;
cleanup:
	if (iomodeClose(_out,_type)){
		perror("verifyDisc close");
	}
	return _ret;
}
signed char verifyDiscFile(const char* _filename){
	FILE* fp = fopen(_filename,"rb");
	if (!fp){
		return -1;
	}
	return verifyDisc(fp,IOMODE_FILE);
}
