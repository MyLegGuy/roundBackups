#define FOLLOWSYMS 0
// most bytes read at once when verifying disc
#define MAXVERIFYHASHBUFF 1000 // 1k
#define EJECTPATH "/usr/bin/eject"
#define MINDISCSPACE 1000000000 // 1 GB

#define PROGRESSUPDATETIME 5

// even more space reserved for metadata to account for bugs and other things that may cause the real calculation to be wrong
// 1 MB
#define COMFYMETADATARESERVED 1000000
