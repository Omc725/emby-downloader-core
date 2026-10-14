CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -fPIC -Iinclude
LDFLAGS ?= -lcurl -lpthread

SRCS = src/platform.cpp \
       src/config.cpp \
       src/client.cpp \
       src/downloader.cpp \
       src/audio_extractor.cpp \
       src/queue.cpp \
       src/engine.cpp \
       src/c_api.cpp

OBJS = $(SRCS:.cpp=.o)

LIB_STATIC = libemby_core.a
LIB_SHARED = libemby_core.so
DEMO_BIN = headless_demo

.PHONY: all clean demo static shared

all: static shared demo

static: $(LIB_STATIC)

shared: $(LIB_SHARED)

$(LIB_STATIC): $(OBJS)
	ar rcs $@ $^

$(LIB_SHARED): $(OBJS)
	$(CXX) -shared -o $@ $^ $(LDFLAGS)

demo: $(DEMO_BIN)

$(DEMO_BIN): examples/headless_demo.cpp $(LIB_STATIC)
	$(CXX) $(CXXFLAGS) -o $@ $< -L. -lemby_core -Wl,-rpath,. $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB_STATIC) $(LIB_SHARED) $(DEMO_BIN)
