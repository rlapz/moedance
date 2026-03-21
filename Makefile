#
# MoeDance - A pretty simple music player
#

TARGET  := moedance
VERSION := 0.0.1

IS_DEBUG ?= 0
PREFIX   := /usr
CC       := cc
CFLAGS   := -std=c11 -Wall -Wextra -D_POSIX_C_SOURCE=200809L \
	    $(shell pkg-config --cflags libpipewire-0.3 libavcodec libavutil libswresample)
LFLAGS   := -lm -lz -lportaudio \
	    $(shell pkg-config --libs libpipewire-0.3 libavformat libavcodec libavutil libswresample)
SRC      := main.c moedance.c tui.c player.c playlist.c kbd.c util.c
OBJ      := $(SRC:.c=.o)

ifeq ($(IS_DEBUG), 1)
	CFLAGS := $(CFLAGS) -g -DDEBUG -O0
	LFLAGS := $(LFLAGS) -fsanitize=address -fsanitize=undefined -fsanitize=leak
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
	@echo "LFLAGS =" $(LFLAGS)
	@echo "CC     =" $(CC)

clean:
	@echo cleaning...
	rm -f $(OBJ) $(TARGET)

#---------------------------------------------------------------------------------------------------#
.PHONY: build clean
