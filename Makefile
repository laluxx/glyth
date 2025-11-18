CC = gcc
CFLAGS = -std=c23 -Wall -Wextra -O2 -march=native `llvm-config --cflags`
LDFLAGS = `llvm-config --ldflags --libs core bitwriter analysis target native --system-libs`

TARGET = glyth
SOURCES = main.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET) *.o *.bc *.ll a.out
