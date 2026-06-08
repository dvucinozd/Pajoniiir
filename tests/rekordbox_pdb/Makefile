# Makefile for Rekordbox PDB parser PC tests
#
# Usage:
#   make                — build test binary
#   make test           — build and run API contract tests
#   make clean          — remove build artifacts
#
# Run with a real Rekordbox USB drive:
#   ./test_pdb F:\PIONEER\rekordbox\export.pdb
#   ./test_pdb F:\PIONEER\rekordbox\export.pdb --limit 20

# WinLibs MinGW-W64 GCC (installed via: winget install BrechtSanders.WinLibs.POSIX.UCRT)
WINLIBS_BIN = $(LOCALAPPDATA)/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin
CC      = $(WINLIBS_BIN)/gcc.exe
CFLAGS  = -Wall -Wextra -Wpedantic \
           -DREKORDBOX_PDB_STANDALONE_TEST \
           -I../../firmware/main-deck-p4/components/library/include

SRCS    = test_pdb.c \
          ../../firmware/main-deck-p4/components/library/rekordbox_pdb.c

TARGET  = test_pdb

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
