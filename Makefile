# Compiler
CC = clang++
CFLAGS = -Wall -g -std=c++14

# Linker
LDFLAGS = -lvirt

# Libraries
LIBS =

# Directories
BINDIR = bin
SRCDIR = src

# Files
SRCS = $(shell find $(SRCDIR) -name '*.cpp')
OBJS = $(patsubst $(SRCDIR)/%.cpp, $(BINDIR)/%.o, $(SRCS))
EXEC = $(BINDIR)/main.out

all: $(SRCS) $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(BINDIR)/%.o: $(SRCDIR)/%.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	/bin/rm -f $(OBJS) $(EXEC)
