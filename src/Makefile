INCS =
LIBS =

CFLAGS = -g -DEXECUTEABLE

all: runner

runner: mnet.o
	gcc $(CFLAGS) $^ -o $@ $(LIBS)

%.o:%.c
	gcc $(CFLAGS) $(INCS) -o $@ -c $<

clean:
	rm -f *.o runner
