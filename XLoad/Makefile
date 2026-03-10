CXX = clang++
CXXFLAGS = -std=c++11 -Wall -O2 -I. -I./FTDI
LDFLAGS = -L./FTDI -Wl,-rpath,./FTDI -lftd2xx

TARGET = xload
SRCS = XLoad.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
