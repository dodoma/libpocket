INCS = -I ~/code/reef/include
LIBS = -L ~/code/reef/src/ -l reef -lpthread

CFLAGS = -g -DEXECUTEABLE

all: runner tm

runner: mnet.o packet.o timer.o client.o binary.o pocket.o callback.o server.o
	gcc $(CFLAGS) $^ -o $@ $(LIBS)

tm: troublemaker.o server.o packet.o pocket.o
	gcc $(CFLAGS) $^ -o $@ $(LIBS)

%.o:%.c
	gcc $(CFLAGS) $(INCS) -o $@ -c $<

clean:
	rm -f *.o runner
