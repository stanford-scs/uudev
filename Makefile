
TARGET = uudev

CPPFLAGS = $$(pkg-config --cflags libudev)
CXXFLAGS = -std=c++17 -g
LIBS = $$(pkg-config --libs libudev)
DESTDIR = /
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
UNITDIR = $(PREFIX)/lib/systemd/user
MANDIR = $(PREFIX)/share/man

all: $(TARGET) $(TARGET).1

$(TARGET): $(TARGET).o
	$(CXX) -o $@ $(TARGET).o $(LIBS)

.cc.o:
	$(CXX) -c $(CXXFLAGS) $(CPPFLAGS) $^
.SUFFIXES: .cc .o

install: all
	mkdir -p "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(UNITDIR)"
	cp $(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"
	strip "$(DESTDIR)$(BINDIR)/$(TARGET)"
	sed -e 's:@BINDIR@:$(BINDIR):' "$(TARGET).service.in" \
		> "$(DESTDIR)$(UNITDIR)/$(TARGET).service~" \
	    && mv -f "$(DESTDIR)$(UNITDIR)/$(TARGET).service~" \
		"$(DESTDIR)$(UNITDIR)/$(TARGET).service"
	if test -f $(TARGET).1; then \
	    mkdir -p "$(DESTDIR)$(MANDIR)" && \
	    cp $(TARGET).1 "$(DESTDIR)$(MANDIR)/"; \
	fi

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"
	rm -f "$(DESTDIR)$(UNITDIR)/$(TARGET).service"
	rm -f "$(DESTDIR)$(MANDIR)/$(TARGET).1"

$(TARGET).1: $(TARGET).1.md
	-pandoc -s -w man -o $@ $@.md

clean:
	rm -f $(TARGET) *.o *~ .*~

.PHONY: all clean install uninstall
