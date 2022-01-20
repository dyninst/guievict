#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <execinfo.h>
#include <sys/un.h>
#define __USE_GNU   /* fcntl's F_SETSIG */
#include <fcntl.h>
#undef __USE_GNU

#include "rt.h"
#include "refun.h"

static struct sctl sctl;

static void
completehijack()
{
	char buf[1024];
	struct sockaddr_un addr;
	int flags, rv, s;
	char *disp;

	/* Get information about this display
	   FIXME: This may not check the right display */
	disp = getenv("DISPLAY");
	if (!disp)
		disp = "localhost:0";
	sctl.orig_xcon = xmalloc(sizeof(*sctl.orig_xcon));
	s = dial_xserver(disp, sctl.orig_xcon);
	if (0 > s) {
		rs_log("cannot dial server %s\n", disp);
		assert(0);
	}
	close(s);

	sprintf(buf, "/tmp/evict.%d.%d", getpid(), getuid());
	unlink(buf);
	sctl.lfd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (0 > sctl.lfd) {
		perror("socket");
		assert(0);
	}

	bzero(&addr, sizeof(addr));
	addr.sun_family = AF_LOCAL;
	strncpy(addr.sun_path, buf, strlen(buf));
	if (0 > bind(sctl.lfd, (struct sockaddr *)&addr, SUN_LEN(&addr))) {
		perror("bind");
		assert(0);
	}

	flags = fcntl(sctl.lfd, F_GETFL);
	assert(flags != -1);
	rv = fcntl(sctl.lfd, F_SETFL, flags|O_ASYNC);
	assert(rv != -1);
	rv = fcntl(sctl.lfd, F_SETSIG, mysig);
	assert(rv != -1);
	rv = fcntl(sctl.lfd, F_SETOWN, getpid());
	assert(rv != -1);
}

static int
checkbt()
{
	void *p[1024];
	int i, n, size;
	static struct modulelist *ml = NULL;

	if (!ml) {
		ml = rf_parse(getpid());
		assert(ml);
	}

	size = 1024;
	n = backtrace(p, size);
	for (i = 0; i < n; i++) {
		char *name = rf_find_address(ml, (unsigned long)p[i]);
#if 1
		rs_log("[%d] %s\n", i, name ? name : "unknown");
#endif
		if (name && in_xlib(name)) {
#if 1
			rs_log("process in Xlib (%s)\n", name);
			rs_log("display is %s\n", getenv("DISPLAY"));
#endif
			return 1;
		}
	}
	return 0;
}

static void (*postpoll)(struct sctl *, char *);
static void *postarg;

static void
pollbt(int sig)
{
	struct itimerval it;
	bzero(&it, sizeof(it));
	if (checkbt())
		return;
	sethijacktimer(&it, NULL); /* cancel poll */
	postpoll(&sctl, postarg);
	free(postarg);
}

static void
inst_pollbt(void (*f)(struct sctl *, char *), void *arg)
{
	struct itimerval it;
	it.it_value.tv_sec = it.it_interval.tv_sec = 0;
	it.it_value.tv_usec = it.it_interval.tv_usec = 50000;
	postpoll = f;
	postarg = xstrdup(arg);
	sethijacktimer(&it, pollbt);
}

struct cmd {
	char *name;
	void (*fn)(struct sctl *, char *arg);
};

static struct cmd
cmd[] = {
	{ "detach", do_detach },
	{ NULL, NULL }
};

void
rtioready(int sig, siginfo_t *info, void *ctx)
{
	char buf[1024];
	int rv, len;
	struct sockaddr_un from;
	struct cmd *cp;
	unsigned long flags;
	char *arg;
	
	/* FIXME: Let's not get more ASYNCs */
	flags = fcntl(sctl.lfd, F_GETFL);
	assert(flags != -1);
	rv = fcntl(sctl.lfd, F_SETFL, flags&(~O_ASYNC));
	assert(rv != -1);

	len = sizeof(from);
	rv = recvfrom(sctl.lfd, buf, sizeof(buf),
		      0, (struct sockaddr*)&from, &len);
	if (0 > rv) {
		perror("recvfrom");
		assert(0);
	}
	if (rv >= sizeof(buf)) {
		rs_log("buffer overflow\n");
		assert(0);
	}
	buf[rv] = '\0';

	for (cp = cmd; cp->name; cp++)
		if (!strncmp(cp->name, buf, strlen(cp->name)))
			break;
		
	if (!cp->name) {
		rs_log("unrecognized command %s\n", buf);
		return;
	}
	arg = buf+strlen(cp->name);
	while (*arg == ' ')
		arg++;
	if (checkbt()) {
		inst_pollbt(cp->fn, arg);
		return;
	} else
		cp->fn(&sctl, arg);
}

void
_init()
{
#if 1
	if (0 > rs_startlog("/tmp/log", RS_LOGAPPEND))
		fprintf(stderr, "cannot start log\n");
#else
	if (0 > rs_startlog(NULL, 0))
		fprintf(stderr, "cannot start log\n");
#endif
	rs_log("welcome to librt.so\n");
	sctl.xsock = findsock();
	if (-1 == sctl.xsock) {
		rs_log("cannot find an X server socket!\n");
		return;
	}
	sig_init();
	completehijack();
}
