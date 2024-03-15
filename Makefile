#
# MoeDance - A pretty simple music player
#

TARGET  = moedance
VERSION = 0.0.1

PREFIX = /usr
CC     = cc
#CFLAGS = -std=c99 -Wall -Wextra -Wno-stringop-overflow -pedantic -O3
CFLAGS = -std=c99 -Wall -Wextra -Wno-stringop-overflow -pedantic -g
OPT    = 

SRC = main.c moedance.c miniaudio.c player.c tui.c util.c
OBJ = $(SRC:.c=.o)

#---------------------------------------------------------------------------------------------------#

all: options $(TARGET)

$(OBJ): config.h

config.h:
	cp config.def.h $(@)

.o: $(TARGET).c
	$(CC) $(CFLAGS) -o $(@) -c $(<)

$(TARGET): $(OBJ)
	$(CC) $(^) -o $(@) -lpthread -lm
#---------------------------------------------------------------------------------------------------#

options:
	@echo $(TARGET) build options:
	@echo "CC     = $(CC)"
	@echo "CFLAGS = $(CFLAGS)"

clean:
	rm -f moedance $(OBJ) moedance-$(VERSION).tar.gz

install: all
	@echo installing executable file to $(PREFIX)/bin

#---------------------------------------------------------------------------------------------------#
.PHONY: all options clean dist install uninstall

