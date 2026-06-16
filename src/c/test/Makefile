# Build and run the Unity test suites for the crclink C code.
#
#   make test    # compile and run the builder and reader suites
#   make clean
#
# Works with gcc/clang on Linux/macOS and mingw gcc on Windows. CI runs `make test`.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O0 -g
SRCDIR   = ..
INCS     = -I$(SRCDIR) -Iunity
UNITY    = unity/unity.c

# On Windows, GNU make hands recipes an empty TMP/TEMP and mingw gcc then falls
# back to the non-writable C:\WINDOWS. Set a writable temp on the compile command
# itself (make's `export` does not reach the recipe here). Detect via uname since
# this make also strips the OS variable. Empty on Linux/macOS.
ifneq (,$(findstring NT,$(shell uname -s)))
TMPENV = TMP=C:/tmp TEMP=C:/tmp
endif

BUILDER_SRCS = $(UNITY) $(SRCDIR)/crc16_xmodem.c $(SRCDIR)/crclink_json.c test_crclink_json.c
READER_SRCS  = $(UNITY) $(SRCDIR)/crclink_json_read.c test_crclink_json_read.c

.PHONY: test clean

test: builder_runner reader_runner
	./builder_runner
	./reader_runner

builder_runner: $(BUILDER_SRCS)
	$(TMPENV) $(CC) $(CFLAGS) $(INCS) $(BUILDER_SRCS) -o builder_runner

# The reader suite is built with floats on so crclink_json_get_float is covered.
reader_runner: $(READER_SRCS)
	$(TMPENV) $(CC) $(CFLAGS) -DCRCLINK_JSON_FLOATS $(INCS) $(READER_SRCS) -o reader_runner

clean:
	rm -f builder_runner reader_runner builder_runner.exe reader_runner.exe
