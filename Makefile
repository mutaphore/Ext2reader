CFLAGS = -g

ext2reader: ext2.c ext2.h program4.c
	gcc $(CFLAGS) -o ext2reader ext2.c program4.c

clean:
	rm -f *.o *.a *.out ext2reader
