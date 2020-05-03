
TARGET = uudev

CPPFLAGS = $$(pkg-config --cflags libudev)
CXXFLAGS = -std=c++17 -g
LIBS = $$(pkg-config --libs libudev)
DESTDIR = /
PREFIX = /usr/local

$(TARGET): $(TARGET).o
	$(CXX) -o $@ $(TARGET).o $(LIBS)

.cc.o:
	$(CXX) -c $(CXXFLAGS) $(CPPFLAGS) $^
.SUFFIXES: .cc .o

install: $(TARGET)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) (DESTDIR)$(PREFIX)/

clean:
	rm -f $(TARGET) *.o *~ .*~

.PHONY: clean install
