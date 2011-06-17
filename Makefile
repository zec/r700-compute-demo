PROGS = step01 step02 step03 step04

CC = gcc
CFLAGS = `pkg-config --cflags --libs libdrm libdrm_radeon` -Wall

all: $(PROGS)

.PHONY: all clean

$(PROGS): %: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(PROGS)
