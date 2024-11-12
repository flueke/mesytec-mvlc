LIBS = -L . -lftd3xx
# Static link to D3XX library, please use Ubuntu 14.04 to compile
#LIBS = -L . -lftd3xx-static -lstdc++-static

# === Linux & macOS ===
DEMO0=streamer
DEMO1=rw
DEMO2=AbortRec
DEMO3=asyncstreamer
DEMO4=async-loopback
DEMO5=FTD3xx_Notification
ifneq ("$(wildcard libftd3xx.dylib)","")
# == macOS ==
CXXLIBS =-lc++
# == End of macOS ==
else
# == Linux ==
LIBS += -pthread -lrt
CXXLIBS = -lstdc++
# == End of Linux ==
endif
# === End of Linux & macOS ===


COMMON_FLAGS = -ffunction-sections -fmerge-all-constants 
COMMON_CFLAGS = -g -O3 -Wall -Wextra $(COMMON_FLAGS)
CFLAGS = -std=c99  $(COMMON_CFLAGS) -D_POSIX_C_SOURCE
CXXFLAGS = -std=c++11 $(COMMON_CFLAGS)

all: $(DEMO0) $(DEMO1) $(DEMO2) $(DEMO3) $(DEMO4) $(DEMO5)
	
$(DEMO0): streamer.o
	$(CC) $(COMMON_FLAGS) -o $@ $^ $(CXXLIBS) $(LIBS)

$(DEMO1): rw.o
	$(CC) $(COMMON_FLAGS) -o $@ $^ $(LIBS)

$(DEMO2): AbortRec.o
	$(CC) $(COMMON_FLAGS) -o $@ $^ $(CXXLIBS) $(LIBS)

$(DEMO3): asyncstreamer.o
	$(CC) $(COMMON_FLAGS) -o $@ $^ $(CXXLIBS) $(LIBS)

$(DEMO4): async-loopback.o
	$(CC) $(COMMON_FLAGS) -o $@ $^ $(CXXLIBS) $(LIBS)

$(DEMO5): FTD3xx_Notification.o
	$(CC) $(COMMON_FLAGS) -o $@ $^ $(CXXLIBS) $(LIBS)

clean:
	-rm -f streamer.o rw.o AbortRec.o asyncstreamer.o async-loopback.o FTD3xx_Notification.o $(DEMO0) $(DEMO1) $(DEMO2) $(DEMO3) $(DEMO4) $(DEMO5)
