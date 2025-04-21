# Makefile for spandsp example programs
#
# Created by: GitHub Copilot
# Date: April 19, 2025

# Define the compiler and compiler flags
CC = gcc
CFLAGS = -Wall -Wextra -g

# Define include directories
INCLUDES = -I.. -I../src

# Define library paths
LIBDIR = -L../src/.libs -L../spandsp-sim/.libs

# Define libraries to link against
LIBS = -lspandsp -ltiff -lm -lsndfile
SIMLIBS = -lspandsp -lspandsp-sim -ltiff -lm -lsndfile

# Default target
all: simple_fax_test gateway_fax_test

# Rule to build the simple_fax_test program
simple_fax_test: simple_fax_test.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBDIR) $(LIBS)

# Rule to build the gateway_fax_test program (needs simulation library)
gateway_fax_test: gateway_fax_test.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBDIR) $(SIMLIBS)

# Rule to clean up built files
clean:
	rm -f simple_fax_test gateway_fax_test

# Help target
help:
	@echo "Available targets:"
	@echo "  all            - Build all example programs (default)"
	@echo "  simple_fax_test - Build the simple fax test program"
	@echo "  gateway_fax_test - Build the gateway fax test program"
	@echo "  clean          - Remove built files"
	@echo "  help           - Display this help message"

.PHONY: all clean help