# ArchAgent — AI-Assisted Code Modification Engine
# See README.md for build instructions and usage.

CC      ?= gcc
CFLAGS  ?= -std=c17 -g -O2 -Wall -Wextra -Werror -pedantic
CPPFLAGS += -Iinclude -D_GNU_SOURCE

SRC_DIR   = src
TEST_DIR  = tests
BIN_DIR   = bin
BUILD_DIR = build
TEST_BIN_DIR = $(BUILD_DIR)/tests

SRCS = $(wildcard $(SRC_DIR)/*.c)
APP_SRCS = $(SRCS)
LIB_SRCS = $(filter-out $(SRC_DIR)/main.c,$(SRCS))
APP_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(APP_SRCS))
LIB_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))

TEST_SRCS = $(wildcard $(TEST_DIR)/test_*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c,$(TEST_BIN_DIR)/%,$(TEST_SRCS))

TARGET = $(BIN_DIR)/archagent

.PHONY: all clean test test-c2asm profile \
        demo-calculator demo-wordcount demo-matrix demo-c2asm gui

all: $(BUILD_DIR) $(BIN_DIR) $(TARGET)

$(TARGET): $(APP_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(TEST_BIN_DIR)/%: $(TEST_DIR)/%.c $(LIB_OBJS) | $(TEST_BIN_DIR) $(TARGET)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(LIB_OBJS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(TEST_BIN_DIR):
	mkdir -p $(TEST_BIN_DIR)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
	rm -rf demo_projects/*/.archagent
	rm -f demo_projects/calculator/calculator
	rm -f demo_projects/wordcount/test_wordcount
	rm -f demo_projects/matrix/bench_matrix

test: all $(TEST_BINS)
	@set -e; \
	for test_bin in $(TEST_BINS); do \
		echo "Running $$test_bin"; \
		$$test_bin; \
	done

test-c2asm: all $(TEST_BIN_DIR)/test_c2asm_lexer $(TEST_BIN_DIR)/test_c2asm_parser $(TEST_BIN_DIR)/test_c2asm_codegen $(TEST_BIN_DIR)/test_c2asm_pipeline
	@set -e; \
	for t in $(TEST_BIN_DIR)/test_c2asm_lexer $(TEST_BIN_DIR)/test_c2asm_parser $(TEST_BIN_DIR)/test_c2asm_codegen $(TEST_BIN_DIR)/test_c2asm_pipeline; do \
		echo "Running $$t"; \
		$$t; \
	done

profile: all
	$(TARGET) --profile

demo-calculator: all
	$(TARGET) --project demo_projects/calculator \
	    --request "Add exponentiation support using ^" \
	    --backend mock --yes --json

demo-wordcount: all
	$(TARGET) --project demo_projects/wordcount \
	    --request "Fix word counting for multiple spaces and newlines" \
	    --backend mock --yes --json

demo-matrix: all
	$(TARGET) --project demo_projects/matrix \
	    --request "Optimise matrix traversal for cache locality" \
	    --backend mock --bench-cmd "make bench" --yes --json

demo-c2asm: all
	$(TARGET) --c2asm-code "int n = 5; int r = 1; while (n > 1) { r = r * n; n = n - 1; } return r;" \
	    --json

gui: all
	@echo "Starting ArchAgent GUI at http://127.0.0.1:5050"
	cd gui && python3 -m flask --app app.py run --host 127.0.0.1 --port 5050