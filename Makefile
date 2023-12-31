TARGET = parse-modules-load
OBJS = main.o libmodprobe.o libmodprobe_ext.o
CFLAGS = -Wall
LDFLAGS =

CC = g++

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(OBJS) $(TARGET)
