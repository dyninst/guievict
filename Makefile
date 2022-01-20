XSRC=$(HOME)/p/xc

CC = gcc
CFLAGS = -Wall -g

HIJACKSRCS = hijack.c refun.c util.c sinj.c code.c
HIJACKOBJS = $(HIJACKSRCS:.c=.o) 

EVICTORSRCS = evictor.c $(HIJACKSRCS)
EVICTOROBJS = $(EVICTORSRCS:.c=.o)

LIBSRCS = rt.c refun.c sock.c util.c signal.c xsw.c detach.c \
          reattach.c x.c font.c xlate.c log.c
LIBOBJS = $(LIBSRCS:.c=.o)

SRCS = $(LIBSRCS) $(EVICTORSRCS) $(HIJACKSRCS) sinjstub.c btoc.c
OBJS = $(SRCS:.c=.o)

INSTALLDIR = $(HOME)/bin

EXTINC= -I$(XSRC)/programs/Xserver/hw/xfree86/common \
	-I$(XSRC)/programs/Xserver/include \
	-I$(XSRC)/include \
	-I$(XSRC)/programs/Xserver/os \
	-I$(XSRC)/programs/Xserver/hw/xfree86 \
	-I/usr/include/X11
EXTFLAGS = $(CFLAGS) $(EXTINC) -DEXTENSION
EXTSRCS = evict.c refun.c
EXTOBJS = $(EXTSRCS:.c=.o)

# this may have redundant entries
DIST = *.h $(SRCS) $(EXTSRCS) README COPYING Makefile libevict.so
DISTNAME = guievict

all: evictor librt.so libevict.so
client: evictor librt.so

.c.o:
	$(CC) $(CFLAGS) -c $<
-include depend

evict.o: evict.c
	$(CC) $(EXTFLAGS) -c $<

libevict.so: $(EXTOBJS)
	gcc -shared -nostartfiles -o $@ $^
	rm evict.o   # so that X loads libevict.so, not evict.o

sinjstub: sinjstub.o
	gcc -o $@ $^ -ldl

btoc: btoc.o
	gcc -o $@ $^

stub.c: btoc sinjstub
	./btoc sinjstub < sinjstub > stub.c

xlc: x.o xlc.o util.o log.o
	gcc -o $@ $^ -L/usr/X11R6/lib -lX11 -lXau -lXext -lXaw -lXt -lm

evictor: $(EVICTOROBJS) stub.c
	gcc -o $@ $^

librt.so: $(LIBOBJS)
	$(CC) -shared -nostartfiles -o librt.so $(LIBOBJS) -L/usr/X11R6/lib -lXau

depend:
	gcc $(INC) -MM $(SRCS) > depend

install: foo
	install foo $(INSTALLDIR)/

clean:
	rm -f *~ core* $(OBJS) depend stub.c

tarball: $(DIST)
	rm -rf $(DISTNAME)
	mkdir $(DISTNAME)
	cp -p -f $(sort $(DIST)) $(DISTNAME)
	tar zcf $(DISTNAME).tar.gz $(DISTNAME)
	rm -rf $(DISTNAME)
