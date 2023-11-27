#---------------------------------------------------------------------------------------------------
# System Programming                       Shell Lab                                    Fall 2021
#
# Makefile for regression testing
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

# make sure SOURCES includes ALL source files required to compile the project
SOURCES=$(wildcard *.c)
TARGETS=$(SOURCES:.c=)

# derived variables
OBJECTS=$(SOURCES:.c=.o)
DEPS=$(SOURCES:.c=.d)

# test harness
DRIVER=./sdriver.pl
TSH=../csapsh
TSHREF=../csapshref
TSHARGS="-p"

# regression traces
TRACEDIR=../trace
TRACES=$(sort $(wildcard $(TRACEDIR)/trace??.txt))
TESTS=$(TRACES:$(TRACEDIR)/trace%.txt=test%)
RTESTS=$(TRACES:$(TRACEDIR)/trace%.txt=rtest%)


#--- rules
.PHONY: doc

all: tools tests rtests

tools: $(TARGETS)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -o $@ -c $<

-include $(DEPS)

clean:
	rm -f $(OBJECTS) $(DEPS)

mrproper: clean
	rm -rf $(TARGETS) doc/html

tests: $(TESTS)
	@echo "Tests completed."

rtests: $(RTESTS)
	@echo "Regression tests completed."

test%: tools
	$(DRIVER) -s $(TSH) -a $(TSHARGS) -t $(TRACEDIR)/$(@:test%=trace%.txt)

rtest%: tools
	$(DRIVER) -s $(TSHREF) -a $(TSHARGS) -t $(TRACEDIR)/$(@:rtest%=trace%.txt)


