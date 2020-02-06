src = $(wildcard *.c) $(wildcard ./borrowed/*.c)
obj = $(src:.c=.o)

LDFLAGS = -lgpgme -lz -lwoarc -lburn
CFLAGS = -g -Wall -Wno-pointer-sign
OUTNAME = a.out

$(OUTNAME): $(obj)
	$(CC) -o $(OUTNAME) $^ $(CFLAGS) $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(obj) $(OUTNAME)

.PHONY: depend
depend:
	makedepend -Y $(src)
# DO NOT DELETE

userIn.o: main.h
verify.o: roundFormat.h config.h iomode.h disc.h
newFileGetter.o: config.h newFileGetter.h main.h borrowed/bsdnftw.h
newFileGetter.o: borrowed/goodLinkedList.h borrowed/filter.h
iomode.o: iomode.h disc.h
disc.o: disc.h
main.o: config.h verify.h iomode.h disc.h userIn.h borrowed/goodLinkedList.h
main.o: borrowed/filter.h roundFormat.h main.h newFileGetter.h
./borrowed/goodLinkedList.o: ./borrowed/goodLinkedList.h
./borrowed/filter.o: ./borrowed/goodLinkedList.h ./main.h ./borrowed/filter.h
