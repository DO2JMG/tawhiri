CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
LDFLAGS = -lm -lpthread

SRCS    = main.c dataset.c interpolate.c models.c solver.c elevation.c httpd.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean check-deps

all: check-deps tawhiri tawhiri-downloader

check-deps:
	@ldconfig -p 2>/dev/null | grep -q libz || \
	  { echo "ERROR: zlib not found. sudo apt install zlib1g-dev"; exit 1; }

tawhiri: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tawhiri-downloader: gfs_download.c
	$(CC) $(CFLAGS) -Wno-format-truncation -o $@ $< -lz -lm

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) tawhiri tawhiri-downloader

main.o:        main.c dataset.h interpolate.h models.h solver.h elevation.h httpd.h
dataset.o:     dataset.c dataset.h
interpolate.o: interpolate.c interpolate.h dataset.h
models.o:      models.c models.h dataset.h interpolate.h elevation.h
solver.o:      solver.c solver.h models.h
elevation.o:   elevation.c elevation.h
httpd.o:       httpd.c httpd.h models.h solver.h dataset.h elevation.h
