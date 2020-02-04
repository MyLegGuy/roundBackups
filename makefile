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

main.o: borrowed/goodLinkedList.h borrowed/filter.h main.h borrowed/bsdnftw.h
./borrowed/goodLinkedList.o: ./borrowed/goodLinkedList.h
./borrowed/filter.o: ./borrowed/goodLinkedList.h ./main.h ./borrowed/filter.h
