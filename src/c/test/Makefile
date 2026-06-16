# Build and run the Unity test suite for the crclink C JSON builder.
#
#   make test    # compile and run
#   make clean
#
# Works with gcc/clang on Linux/macOS and mingw gcc on Windows. CI runs `make test`.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O0 -g

# On Windows, GNU make hands recipes an empty TMP/TEMP and mingw gcc then falls
# back to the non-writable C:\WINDOWS. Set a writable temp on the compile command
# itself (make's `export` does not reach the recipe here). Detect via uname since
# this make also strips the OS variable. Empty on Linux/macOS.
ifneq (,$(findstring NT,$(shell uname -s)))
TMPENV = TMP=C:/tmp TEMP=C:/tmp
endif
SRCDIR   = ..
INCS     = -I$(SRCDIR) -Iunity
SOURCES  = unity/unity.c \
           $(SRCDIR)/crc16_xmodem.c \
           $(SRCDIR)/crclink_json.c \
           test_crclink_json.c
RUNNER   = test_runner

.PHONY: test clean

test: $(RUNNER)
	./$(RUNNER)

$(RUNNER): $(SOURCES)
	$(TMPENV) $(CC) $(CFLAGS) $(INCS) $(SOURCES) -o $(RUNNER)

clean:
	rm -f $(RUNNER) $(RUNNER).exe
