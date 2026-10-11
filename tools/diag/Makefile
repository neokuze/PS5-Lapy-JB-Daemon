include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ELF := lapy_hdd_diag.elf

all: $(ELF)

$(ELF): diag.c
	$(CC) -O2 -Wall -o $@ $< -lkernel_sys -lSceNotification

clean:
	rm -f $(ELF)

.PHONY: all clean
