SHELL := /bin/bash

CC       ?= gcc
CFLAGS   ?= -Wall -fdiagnostics-color=always -Werror -Wfatal-errors
CPPFLAGS ?=
STRIP    ?= strip

# Sources - compiled paths
SRC         = ./src
CMP         = $(SRC)/cmp
CMP_ARCH    = $(CMP)/$(shell uname -m)

EXES  = modbus-server modbus-client

.PHONY: all clean create-cmp-dir doc

all: create-cmp-dir $(EXES)

install: cleaninst

# =============================================
# Compile Sections
# =============================================

modbus-server: $(SRC)/modbus-server.c $(SRC)/mbt-srv.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -D_GNU_SOURCE  -o $(CMP_ARCH)/$@  $^ $(LDFLAGS) -lmodbus -pthread -lrt
	$(STRIP) $(CMP_ARCH)/$@
	@echo "Compiled $(CMP_ARCH)/$@"

modbus-client: $(SRC)/modbus-client.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -D_GNU_SOURCE  -o $(CMP_ARCH)/$@  $^ $(LDFLAGS) -lmodbus -pthread -lrt
	$(STRIP) $(CMP_ARCH)/$@
	@echo "Compiled $(CMP_ARCH)/$@"

doc:
	@doxygen doc/Doxyfile
	@sphinx-build -M html doc/ doc/build

# =============================================
# Cleaning and installing
# =============================================

clean:
	rm -rf $(CMP)/*

create-cmp-dir:
	@mkdir -p $(CMP_ARCH)
