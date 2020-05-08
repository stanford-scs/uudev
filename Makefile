
TARGET = uudev

CPPFLAGS = $$(pkg-config --cflags libudev)
CXXFLAGS = -std=c++17 -g
LIBS = $$(pkg-config --libs libudev)
DESTDIR = /
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
UNITDIR = $(PREFIX)/lib/systemd/user
MANDIR = $(PREFIX)/share/man

$(TARGET): $(TARGET).o
	$(CXX) -o $@ $(TARGET).o $(LIBS)

.cc.o:
	$(CXX) -c $(CXXFLAGS) $(CPPFLAGS) $^
.SUFFIXES: .cc .o

install: $(TARGET)
	mkdir -p "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(UNITDIR)"
	cp $(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"
	strip "$(DESTDIR)$(BINDIR)/$(TARGET)"
	sed -e 's:@BINDIR@:$(BINDIR):' "$(TARGET).service.in" \
		> "$(DESTDIR)$(UNITDIR)/$(TARGET).service~" \
	    && mv -f "$(DESTDIR)$(UNITDIR)/$(TARGET).service~" \
		"$(DESTDIR)$(UNITDIR)/$(TARGET).service"

$(TARGET).1: $(TARGET).1.md
	pandoc -s -w man -o $@ $@.md

clean:
	rm -f $(TARGET) *.o *~ .*~

.PHONY: clean install
