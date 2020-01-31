#define IOMODE_FILE 0
#define IOMODE_DISC 1

// returns number written
ssize_t iomodeWrite(void* _out, char _type, const void* buffer, size_t size);
// returns 0 if worked, -2 on fail
signed char iomodeWriteFail(void* _out, char _type, const void* buffer, size_t size);
signed char iomodePutc(void* _out, char _type, unsigned char _byte);
signed char iomodeClose(void* _out, char _type);
// works like fread but size is 1
size_t iomodeRead(void* _out ,char _type, void* _dest, size_t nmemb);
// works like fgetc
int iomodeGetc(void* _out, char _type);
///
signed char write16(void* _out, char _type, uint16_t n);
signed char write32(void* _out, char _type, uint32_t n);
signed char write64(void* _out, char _type, uint64_t n);
///
signed char read32(void* _out, char _type, uint32_t* n);
