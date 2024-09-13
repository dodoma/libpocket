INCS = -I ~/code/reef/include
LIBS = -L ~/code/reef/src/ -l reef -lpthread

CFLAGS = -g -DEXECUTEABLE

all: runner

runner: mnet.o packet.o timer.o client.o pocket.o callback.o server.o
	gcc $(CFLAGS) $^ -o $@ $(LIBS)

%.o:%.c
	gcc $(CFLAGS) $(INCS) -o $@ -c $<

clean:
	rm -f *.o runner
