TARGET=areader
CFLAGS=-Wall -ggdb
LDFLAGS=-lrt
	
sources= async-reader.c
objects= $(sources:.c=.o) main.o
headers= $(sources:.c=.h)

all: $(TARGET) 

$(objects): $(headers) Makefile

$(TARGET): $(objects) 
	gcc $(LDFLAGS) -o $(@) $(objects)

clean:
	rm -f *.o $(TARGET) 
