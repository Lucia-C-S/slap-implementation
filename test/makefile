# test/Makefile
# Run from the slap/ root: make -f test/Makefile test
# You run make -f test/Makefile test from your terminal.
# No hardware, no FreeRTOS, no flashing.
# If it compiles and all lines print [PASS],
# the core logic is correct.


CC      = gcc
CFLAGS  = -Wall -Wextra -g -I.
LDFLAGS = -pthread

SRCS_COMMON = osal/ports/osal_posix.c

test_databank: test/test_databank.c slap_databank.c $(SRCS_COMMON)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

test_packet: test/test_packet.c slap_packet.c ecf.c \
             slap_databank.c $(SRCS_COMMON)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

test: test_databank test_packet
	./test_databank
	./test_packet

clean:
	rm -f test_databank test_packet