CC = gcc
CFLAGS = -Wall -Wextra -Werror -pthread -lrt -I./src
SRC_DIR = src
OBJ_DIR = obj

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

TARGET = server

all: $(TARGET)

debug: CFLAGS += -g -O0
debug: clean all

release: CFLAGS += -O3
release: clean all

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

test:
	bash tests/test_load.sh

valgrind: all
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)

helgrind: all
	valgrind --tool=helgrind ./$(TARGET)
