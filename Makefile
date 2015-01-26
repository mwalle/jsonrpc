TOPDIR=$(shell pwd)

jsonrpc_SOURCES := $(wildcard *.c)
jsonrpc_HEADERS := $(wildcard *.h)
jsonrpc_OBJECTS := $(jsonrpc_SOURCES:.c=.o)

CFLAGS += -I$(TOPDIR) -O2 -Wall -Werror

JANSSON_LIBS := $(shell pkg-config --libs jansson)

all: libjsonrpc.a

%.o: %.c $(jsonrpc_HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

libjsonrpc.a: $(jsonrpc_OBJECTS)
	$(AR) rcs $@ $^

test/handle_stdio: test/handle_stdio.c libjsonrpc.a $(jsonrpc_HEADERS)
	$(CC) $(JANSSON_LIBS) -L$(TOPDIR) $(CFLAGS) -o $@ $< -ljsonrpc

test: test/handle_stdio
	cd $(TOPDIR)/test; nosetests test_jsonrpc.py

clean:
	rm -f $(jsonrpc_OBJECTS) libjsonrpc.a
	rm -f test/handle_stdio

.PHONY: all clean test
