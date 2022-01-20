#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <syscall.h>
#include <sys/time.h>
#include <netinet/in.h>

#include "guickpt.h"

struct fcntlstate {
	unsigned long flags;
	unsigned long owner;
	unsigned long sig;
};

struct xvisual {
	unsigned long id;
	int class;
	int depth;
	unsigned long red_mask, blue_mask, green_mask;
	unsigned long bits_per_rgb, entries;
};

struct xcon {
	xConnSetupPrefix prefix;
	xConnSetup setup;
	unsigned long rootid, rootvis, rootdepth;
	unsigned long defaultcmap;
	int num_vis;
	struct xvisual *vis;
	unsigned long vicmajor;
};

struct sctl {
	int lfd;      /* evictor communication */
	int xsock;    /* socket to window server */
	int major;    /* extension major opcode */
	unsigned long nextid; /* available client-side ids */
	struct guickpt *ckpt; /* the jewels */
	struct fcntlstate fcntl;
	struct xcon *orig_xcon;  /* original X server */
	struct xcon *xcon;       /* current X server */
};

/* rt.c */
void rtioready(int sig, siginfo_t *info, void *ctx);

/* util.c */
void *xmalloc(size_t size);
void *xrealloc(void *p, size_t size);
char *xstrdup(char *str);
int xwrite(int sd, const void *buf, size_t len);
int xread(int sd, void *buf, size_t len);
int xpad(int len);
void xreadpad(int sd, size_t len);
int xwritepad(int sd, const void *buf, size_t len);
void tv_diff(const struct timeval *a, const struct timeval *b,
	     struct timeval *c);
int parse_addr(const char *s, struct in_addr *addr);

/* signal.c */
int mysig;
void sig_init();
void sethijacktimer(struct itimerval *it, void (*handler)(int));

/* sock.c */
int findsock();

/* rpc.c */
int hijackrpc(int fd);

/* x.c */
void readxreply(int fd, char *rep);
void readxreplyignore(int fd, char *rep);
int xauth(char *disp, char **type, char **data, int *len);
int disp_to_saddr(char *display, struct sockaddr_in *saddr);
int dial_xserver(char *disp, struct xcon *xcon);

/* xsw.c */
int in_xlib(char *name);

/* detach.c */
void do_detach(struct sctl *sctl, char *arg);

/* reattach.c */
void waitforreattach(struct sctl *sctl);
unsigned long nextid(struct sctl *sctl);

/* font.c */
void getfont(struct sctl *sctl, unsigned long id, XFontStruct *fs, unsigned long *nchar);
void printfonts();
void initfont(struct sctl *sctl);
char *matchfont(struct sctl *sctl, XFontStruct *fs);

/* xlate.c */
void doxlate1(int a, int s, struct sctl *sctl);
void doxlate2(int a, int s, struct sctl *sctl);
void xlate_xResourceReq(char *p);
void initseq(unsigned long cbase, unsigned long sbase, unsigned long diff);

/* log.c */
enum {
	RS_LOGSTDERR = 1,
	RS_LOGNOLOG = 2,
	RS_LOGPRECISETIME = 4,
	RS_LOGAPPEND = 8
};
extern int geverbose;
void rs_log(char *fmt, ...);
void rs_tty_print(char *fmt, ...);
int rs_startlog(const char *logfilename, int flags);
void rs_closelog();
int rs_logfileno();
void rs_logerror(char *s);
