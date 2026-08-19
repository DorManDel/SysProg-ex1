# MAKEFILE

# Wall- bunch of warnings
# Wextra- extra warnings
# std=gnull1- C11 regulation + GNU extensions
# O2- compiler optimizations LVL2

CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -O2

# mycp.c --gcc--> mycp

TARGET = my_cp
SOURCE = my_cp.c

# default choose first target

all: $(TARGET)

# needs actual tab to work!
# make follows dependencies - compile only what changed 
# not rebuilding if its unneccessary

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET)

# --- For Tests ONLY

test: check-build
	./tests/run_all.sh
check-build:
	$(CC) $(CFLAGS) -Werror $(SOURCE) -o $(TARGET)

# sanitize:
# 	$(CC) -Wall -Wextra -std=gnu11 -O1 -g \
# 		-fsanitize=address,undefined \
# 		-fno-omit-frame-pointer \
# 		$(SOURCE) -o $(TARGET)

# ---

clean:
	rm -f $(TARGET)

# PHONY: clean+all = logical command names - not files

.PHONY: all clean test check-build

