CC ?= gcc
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
WWWDIR ?= /var/www/ahsd

TARGET := ahsd
TEST_TARGET := build/test_runner
SRC_DIR := src
TEST_DIR := tests

SRCS := $(wildcard $(SRC_DIR)/*.c)
LIB_SRCS := $(filter-out $(SRC_DIR)/main.c,$(SRCS))
TEST_SRCS := $(wildcard $(TEST_DIR)/*.c)

BASE_CFLAGS := -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
               -Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes \
               -MMD -MP -I$(SRC_DIR) -I$(TEST_DIR) \
               -DAHSD_DEFAULT_WWW_ROOT=\"$(WWWDIR)\"

RELEASE_DIR := build/release
DEBUG_DIR := build/debug

RELEASE_CFLAGS := $(BASE_CFLAGS) -O2 -DNDEBUG
DEBUG_CFLAGS := $(BASE_CFLAGS) -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer
DEBUG_LDFLAGS := -fsanitize=address,undefined

RELEASE_OBJS := $(patsubst $(SRC_DIR)/%.c,$(RELEASE_DIR)/%.o,$(SRCS))
DEBUG_OBJS := $(patsubst $(SRC_DIR)/%.c,$(DEBUG_DIR)/%.o,$(SRCS))
TEST_LIB_OBJS := $(patsubst $(SRC_DIR)/%.c,$(DEBUG_DIR)/%.o,$(LIB_SRCS))
TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(DEBUG_DIR)/tests/%.o,$(TEST_SRCS))

.PHONY: all release debug test install uninstall fmt clean

all: release

release: $(TARGET)

$(TARGET): $(RELEASE_OBJS)
	$(CC) $(RELEASE_OBJS) -o $@

$(RELEASE_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(RELEASE_CFLAGS) -c $< -o $@

debug: $(TARGET)-debug

$(TARGET)-debug: $(DEBUG_OBJS)
	$(CC) $(DEBUG_LDFLAGS) $(DEBUG_OBJS) -o $@

$(DEBUG_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(DEBUG_CFLAGS) -c $< -o $@

$(DEBUG_DIR)/tests/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(DEBUG_CFLAGS) -c $< -o $@

$(TEST_TARGET): $(TEST_LIB_OBJS) $(TEST_OBJS)
	$(CC) $(DEBUG_LDFLAGS) $(TEST_LIB_OBJS) $(TEST_OBJS) -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

install: release
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -d $(DESTDIR)$(WWWDIR)
	@if [ ! -f $(DESTDIR)$(WWWDIR)/index.html ]; then \
		printf '<!doctype html><html><body><h1>ahsd</h1></body></html>\n' > $(DESTDIR)$(WWWDIR)/index.html; \
	fi

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

fmt:
	clang-format -i $(SRC_DIR)/*.c $(SRC_DIR)/*.h $(TEST_DIR)/*.c $(TEST_DIR)/*.h

clean:
	rm -rf build $(TARGET) $(TARGET)-debug

-include $(RELEASE_OBJS:.o=.d) $(DEBUG_OBJS:.o=.d) $(TEST_OBJS:.o=.d)
