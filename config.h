#define FOLLOWSYMS 0
// most bytes read at once when verifying disc
#define MAXVERIFYHASHBUFF 1000 // 1k
#define EJECTPATH "/usr/bin/eject"
#define MINDISCSPACE 1000000000 // 1 GB

#define PROGRESSUPDATETIME 5

// even more space reserved for metadata to account for bugs and other things that may cause the real calculation to be wrong
// this must not be 0. my calculation for the pgp file output size is always too small by (here are some tests:) 130 bytes (7GB file) 19 bytes (25000 bytes file) 37 bytes (4GB file) 164 (25GB file)
// my 25 GB discs have dug 346944 bytes (347 kb) into this comfy padding before.
// 5 MB
#define COMFYMETADATARESERVED 5000000
