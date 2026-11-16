CC      := gcc
TARGET  := ahsd

SRC_DIR := src
BLD_DIR := build

SRCS    := $(wildcard $(SRC_DIR)/*.c)
OBJS    := $(patsubst $(SRC_DIR)/%.c, $(BLD_DIR)/%.o, $(SRCS))
DEPS    := $(OBJS:.o=.d)

BASE_FLAGS := -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
              -Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes \
              -MMD -MP -I$(SRC_DIR)

REL_FLAGS  := -O2 -DNDEBUG
DBG_FLAGS  := -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer

CFLAGS  ?= $(BASE_FLAGS) $(REL_FLAGS)
LDFLAGS ?=

.PHONY: all debug release format clean

all: release

release: CFLAGS := $(BASE_FLAGS) $(REL_FLAGS)
release: $(TARGET)

debug: CFLAGS := $(BASE_FLAGS) $(DBG_FLAGS)
debug: LDFLAGS += -fsanitize=address,undefined
debug: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BLD_DIR)/%.o: $(SRC_DIR)/%.c | $(BLD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BLD_DIR):
	mkdir -p $(BLD_DIR)

format:
	clang-format -i $(SRC_DIR)/*.c $(SRC_DIR)/*.h

clean:
	rm -rf $(BLD_DIR) $(TARGET)

-include $(DEPS)
