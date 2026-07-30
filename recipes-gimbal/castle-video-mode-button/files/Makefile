CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?=
LDLIBS += -lgpiod

TARGET = castle-video-mode-button

SRCS = castle-video-mode-button.c
OBJS = $(SRCS:.c=.o)

PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin

all: $(TARGET)

$(TARGERT): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all install clean
