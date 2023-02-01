TARGET := test
LD_SCRIPT := test.ld
CC := gcc
CFLAGS := -Wl,-Map=test.map -Wall

SRC := test.c
OBJ := $(patsubst %.c, %.o, $(SRC))

$(TARGET):$(OBJ)
	$(CC) $(OBJ) -o $@ $(CFLAGS) -T$(LD_SCRIPT)

%.o:%.c
	$(CC) -c $< -o $@ $(CFLAGS)

.PHONY: clean
clean:
	find ./ -name "*.o" | xargs rm -f
	find ./ -name "*.map" | xargs rm -f
	find ./ -name "$(TARGET)" | xargs rm -f