CFLAGS = -Wall -g
LFLAGS = -lm
CC = gcc

all: fsck mkfs

objects = common.o ovl.o lib.o feature.o mount.o path.o overlayfs.o
fsck-objects = fsck.o check.o $(objects)
mkfs-objects = mkfs.o $(objects)

fsck: $(fsck-objects)
	$(CC) $(LFLAGS) $(fsck-objects) -o fsck.overlay

mkfs: $(mkfs-objects)
	$(CC) $(LFLAGS) $(mkfs-objects) -o mkfs.overlay

.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o fsck.overlay mkfs.overlay
	rm -rf bin

install: all
	mkdir bin
	cp fsck.overlay mkfs.overlay bin
