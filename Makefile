#
# MoeDance - A pretty simple music player
#

TARGET  := moedance
VERSION := 0.0.1

IS_DEBUG ?= 0
PREFIX   := /usr
CC       := cc
CFLAGS   := -std=c99 -Wall -Wextra -D_XOPEN_SOURCE=700 -pedantic -I/usr/include/ffmpeg
LFLAGS   := -lpthread -lm -lavformat -lavutil
SRC      := main.c moedance.c tui.c player.c playlist.c kbd.c util.c
OBJ      := $(SRC:.c=.o)

ifeq ($(IS_DEBUG), 1)
	CFLAGS := $(CFLAGS) -g -DDEBUG -O0
	LFLAGS := $(LFLAGS) -fsanitize=address -fsanitize=undefined
else
	CFLAGS := $(CFLAGS) -O3
endif

#---------------------------------------------------------------------------------------------------#

build: options $(TARGET)

$(OBJ): config.h

config.h:
	cp config.def.h $(@)

$(TARGET).o: $(TARGET).c
	@printf "\n%s\n--------------------\n" "Compiling..."
	$(CC) $(CFLAGS) -c -o $(@) $(<)

$(TARGET): $(OBJ)
	@printf "\n%s\n--------------------\n" "Linking..."
	$(CC) -o $(@) $(^) $(LFLAGS)


#---------------------------------------------------------------------------------------------------#

options:
	@echo \'$(TARGET)\' build options:
	@echo "CFLAGS =" $(CFLAGS)
	@echo "CC     =" $(CC)

clean:
	@echo cleaning...
	rm -f $(OBJ) $(TARGET)

#---------------------------------------------------------------------------------------------------#
.PHONY: build clean
