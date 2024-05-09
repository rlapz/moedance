#
# MoeDance - A pretty simple music player
#

TARGET  = moedance
VERSION = 0.0.1

PREFIX = /usr
CC     = cc
#CFLAGS = -std=c99 -Wall -Wextra -D_XOPEN_SOURCE=700 -pedantic -O3
CFLAGS = -std=c99 -Wall -Wextra -D_XOPEN_SOURCE=700 -pedantic -I/usr/include/ffmpeg -O3
OPT    = 

SRC = main.c moedance.c tui.c player.c playlist.c kbd.c util.c
OBJ = $(SRC:.c=.o)

#---------------------------------------------------------------------------------------------------#

all: options $(TARGET)

$(OBJ): config.h

config.h:
	cp config.def.h $(@)

.o: $(TARGET).c
	$(CC) $(CFLAGS) -o $(@) -c $(<)

$(TARGET): $(OBJ)
	#$(CC) $(^) -o $(@) -lpthread -lm
	$(CC) $(^) -lpthread -lm -lavformat -lavutil -o $(@)
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

