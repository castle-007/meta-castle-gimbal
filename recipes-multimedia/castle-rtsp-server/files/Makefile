CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?=

TARGET = castle-rtsp-server
SRCS = castle-rtsp-server.c
OBJS = $(SRCS:.c=.o)

PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin

PKG_CONFIG := $(shell command -v pkg-config 2>/dev/null || echo pkg-config)
PKG_CONFIG ?= $(HOST_PREFIX)pkg-config

GST_CFLAGS = $(shell $(PKG_CONFIG) --cflags \
		gstreamer-1.0 gstreamer-rtsp-server-1.0)
GST_LIBS = $(shell $(PKG_CONFIG) --libs \
		gstreamer-1.0 gstreamer-rtsp-server-1.0)

all: $(TARGET)

$(TARGET): $(OBJS)
		$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(GST_LIBS)

%.o: %.c
		$(CC) $(CFLAGS) $(GST_CFLAGS) -c -o $@ $<

install: $(TARGET)
		install -d $(DESTDIR)$(BINDIR)
		install -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
		rm -f $(TARGET) $(OBJS)

.PHONY: all install clean
