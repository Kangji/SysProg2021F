#---------------------------------------------------------------------------------------------------
# System Programming                       Shell Lab                                    Fall 2021
#
# Makefile
#
# GNU make documentation: https://www.gnu.org/software/make/manual/make.html
#
# Typically, the only thing you need to modify in this Makefile is the list of source files.
#

#--- variable declarations

# C compiler and compilation flags
CC=gcc
CFLAGS=-Wall -O2 -g
DEPFLAGS=-MMD -MP

STRIP=strip
#STRIP=echo
STRIPFLAGS=-R .comment -R .note -R .note.ABI-tag -R .debug*

# make sure SOURCES includes ALL source files required to compile the project
SOURCES=csapsh.c
TARGETS=$(SOURCES:.c=)

# derived variables
OBJECTS=$(SOURCES:.c=.o)
DEPS=$(SOURCES:.c=.d)

#--- rules
.PHONY: doc

all: $(TARGETS) csapsh

csapshref: csapsh
	cp csapsh csapshref
	$(STRIP) $(STRIPFLAGS) $@

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -o $@ -c $<

-include $(DEPS)

doc: $(SOURES) $(wildcard $(SOURCES:.c=.h))
	doxygen doc/Doxyfile

clean:
	rm -f $(OBJECTS) $(DEPS)

mrproper: clean
	rm -rf $(TARGETS) csapshref doc/html
