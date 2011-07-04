PROGS = step01 step02 step03 step04

CC = gcc
CFLAGS = `pkg-config --cflags --libs libdrm libdrm_radeon` -Wall

all: $(PROGS)

.PHONY: all clean

$(PROGS): %: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(PROGS)

step05: r600_reg.h r600_reg_auto_r6xx.h r600_reg_r6xx.h r600_reg_r7xx.h
