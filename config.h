#define FOLLOWSYMS 0
// most bytes read at once when verifying disc
#define MAXVERIFYHASHBUFF 1000 // 1k
#define EJECTPATH "/usr/bin/eject"
#define MINDISCSPACE 1000000000 // 1 GB
// extra space that we set aside for the extra metadata not accounted for in the file data itself.
// this includes all the worried archive metadata, all the gpg metadata, and all the round backup metadta.
// if i assume consistent gpg behavior, it is possible to calculate how much space the metadata will take
// but because of how many layers there are, it's not worth it. so i just pick a big number and pray.
// 50 MB. this should probably maybe be enough space.
#define METADATARESERVEDSPACE ((long)(50*1000*1000))

#define PROGRESSUPDATETIME 5
