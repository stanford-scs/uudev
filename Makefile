
TARGET = uudev

CPPFLAGS = $$(pkg-config --cflags libudev)
CXXFLAGS = -std=c++17 -g
LIBS = $$(pkg-config --libs libudev)
DESTDIR = /
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
UNITDIR = $(PREFIX)/lib/systemd/user
MANDIR =

all: $(TARGET) $(TARGET).1

$(TARGET): $(TARGET).o
	$(CXX) -o $@ $(TARGET).o $(LIBS)

.cc.o:
	$(CXX) -c $(CXXFLAGS) $(CPPFLAGS) $^
.SUFFIXES: .cc .o

install: all
	install -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(UNITDIR)"
	install -s $(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"
	sed -e 's:@BINDIR@:$(BINDIR):' "$(TARGET).service.in" \
		> "$(DESTDIR)$(UNITDIR)/$(TARGET).service~" \
	    && mv -f "$(DESTDIR)$(UNITDIR)/$(TARGET).service~" \
		"$(DESTDIR)$(UNITDIR)/$(TARGET).service"
	if test -f $(TARGET).1; then \
	    mandir="$(MANDIR)"; \
	    if test -z "$$mandir"; then \
		mandir="$(PREFIX)/man"; \
	        if test ! -d "$(DESTDIR)$$mandir"; then \
		    mandir="$(PREFIX)/share/man"; \
		fi; \
	    fi; \
	    install -d "$(DESTDIR)$$mandir/man1" && \
		install $(TARGET).1 "$(DESTDIR)$$mandir/man1/"; \
	fi

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"
	rm -f "$(DESTDIR)$(UNITDIR)/$(TARGET).service"
	if test -n "$(MANDIR)"; then					   \
	    rm -f "$(DESTDIR)$(MANDIR)/man1/$(TARGET).1";		   \
	else								   \
	    rm -f "$(DESTDIR)$(PREFIX)/"{man,share/man}/"man1/$(TARGET).1"; \
	fi

$(TARGET).1: $(TARGET).1.md
	-pandoc -s -w man -o $@ $@.md

clean:
	rm -f $(TARGET) *.o *~ .*~

.PHONY: all clean install uninstall
