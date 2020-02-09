#define METADATAMAGIC "ROUNDEND"
#define ROUNDVERSIONNUM 1

// the maximum number of session key packets that can be copied for the sessionKeyPacketBackup
#define MAXSESSIONKEYPACKETSCOPIED 2
// i've seen lengths of 268 and also around 240
// so just be safe
#define ASSUMEDSESSIONKEYPACKETLEN 500
//
#define ROUNDMETADATABASEOVERHEAD (								\
		1+														\
		strlen(METADATAMAGIC)+									\
		1+														\
		8+														\
		4+														\
		8+														\
		ASSUMEDSESSIONKEYPACKETLEN*MAXSESSIONKEYPACKETSCOPIED	\
		)

// on my system, the partial continue packets have this much data. therefor i assume this will be true on all systems.
#define PARTIALCONTINUE 8192
// how much extra metadata is there if we add this many bytes to the pgp file?
// this can be found by the number of partial length continue packet headers there are
#define GPGEXTRAMETADATAOVERHEAD(byteCount) (((byteCount)/PARTIALCONTINUE+1))
