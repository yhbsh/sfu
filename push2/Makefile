CC = clang -O3 -march=native
LIBS = -lswscale -lavutil -lx264 -lx265 -lc++ -laom
FW = -framework AVFoundation -framework Foundation -framework CoreVideo -framework CoreMedia -framework CoreGraphics

SRC = $(wildcard *.c *.m)
BIN = $(SRC:.c=)
BIN := $(BIN:.m=)

all: $(BIN)

%: %.c common.h
	$(CC) -o $@ $< $(LIBS) $(FW)

%: %.m common.h
	$(CC) -o $@ $< $(LIBS) $(FW)

clean:
	rm -f $(BIN)
