/* TODO:
   mysig overloading 
*/

/* welcome to gnu */
typedef void (*sighandler_t)(int);

#include "refun.h"
#include "rt.h"

int mysig = SIGSYS;

static int
xsigprocmask(int how, sigset_t *iset, sigset_t *old)
{
	sigdelset(iset, mysig);
	return syscall(SYS_sigprocmask, how, iset, old);
}

static int
xsigsuspend(sigset_t *mask)
{
	sigaddset(mask, mysig);
	return syscall(SYS_sigsuspend, mask);
}

/* timer routines */
enum {
	NONE, US, THEM
};
static int nexttimer;  /* NONE, US, or THEM */
static struct timeval us;
static struct timeval usreset;
static sighandler_t ushandler;
static struct sigaction themaction;
static struct timeval them;
static struct timeval themreset;

static int
tv_cmp(const struct timeval *a,
	   const struct timeval *b)
{
	if (a->tv_sec < b->tv_sec)
		return -1;
	if (a->tv_sec > b->tv_sec)
		return 1;
	if (a->tv_usec < b->tv_usec)
		return -1;
	if (a->tv_usec > b->tv_usec)
		return 1;
	return 0;
}

static int
tv_zero(const struct timeval *tv)
{
	return (!tv->tv_sec && !tv->tv_usec);
}

int
xsigaction(int sig, struct sigaction *act, struct sigaction *oldact)
{
	if (sig == SIGALRM) {
		if (oldact)
			*oldact = themaction;
		themaction = *act;
	} else if (sig != mysig)
		return syscall(SYS_sigaction, sig, act, oldact);
	rs_log("ignoring sigaction for %d\n", mysig);
	return 0;
}

static sighandler_t
xsignal(int sig, sighandler_t handler)
{
	sighandler_t h;
	if (sig == SIGALRM) {
		themaction.sa_flags &= ~SA_SIGINFO;
		h = themaction.sa_handler;
		themaction.sa_handler = handler;
		return h;
	} else if (sig != mysig)
		return (sighandler_t) syscall(SYS_signal, sig, handler);
	rs_log("ignoring sigaction for %d\n", mysig);
	return 0;
}

static int
resettimer()
{
	int rv;
	struct itimerval next;
	bzero(&next, sizeof(next));
	if (tv_zero(&us) && tv_zero(&them)) {
		nexttimer = NONE;
	} else  if (tv_zero(&us)) {
		next.it_value = them;
		nexttimer = THEM;
	} else if (tv_zero(&them)) {
		next.it_value = us;
		nexttimer = US;
	} else {
		if (tv_cmp(&us, &them) < 0) {
			next.it_value = us;
			nexttimer = US;
		} else {
			next.it_value = us;
			nexttimer = US;
		}
	}
	rv = syscall(SYS_setitimer, ITIMER_REAL, &next, NULL);
	return rv;
}

static void
sigalrm(int sig, siginfo_t *info, void *ctx)
{
	int rv;
	if (nexttimer == US) {
		ushandler(sig);
		us = usreset;
	} else if (nexttimer == THEM) {
		if (themaction.sa_flags&SA_SIGINFO) {
			themaction.sa_sigaction(sig, info, ctx);
		} else {
			if (themaction.sa_handler == SIG_DFL)
				abort();
			else if (themaction.sa_handler != SIG_IGN)
				themaction.sa_handler(sig);
		}
		them = themreset;
	} else
		assert(0);
	rv = resettimer();
	assert(rv != -1);
}

static int
xsetitimer(int which, const struct itimerval *val, struct itimerval *old)
{
	sigset_t s, o;
	int rv;

	if (which != ITIMER_REAL)
		return syscall(SYS_setitimer, which, val, old);

	sigfillset(&s);
	xsigprocmask(SIG_SETMASK, &s, &o);
	if (old) {
		if (tv_zero(&them))
			bzero(old, sizeof(*old));
		else {
			old->it_value = them;
			old->it_interval = themreset;
		}
	}
	them = val->it_value;
	themreset = val->it_interval;
	rv = resettimer();
	xsigprocmask(SIG_SETMASK, &o, NULL);
	return rv;
}

unsigned int
xalarm(unsigned int seconds)
{
	struct itimerval cur, old;
	int rv;
	bzero(&cur, sizeof(cur));
	cur.it_value.tv_sec = seconds;
	rv = xsetitimer(ITIMER_REAL, &cur, &old);
	assert(rv != -1);
	return cur.it_value.tv_sec;
}

static int
xgetitimer(int which, struct itimerval *val)
{
	sigset_t s, o;
	if (which != ITIMER_REAL)
		return syscall(SYS_getitimer, which, val);
	sigfillset(&s);
	xsigprocmask(SIG_SETMASK, &s, &o);
	if (val) {
		val->it_value = them;
		val->it_interval = themreset;
	}
	xsigprocmask(SIG_SETMASK, &o, NULL);
	return 0;
}

void
sethijacktimer(struct itimerval *it, void (*handler)(int))
{
	us = it->it_value;
	usreset = it->it_interval;
	ushandler = handler;
	resettimer();
}

static void
hijacktimer()
{
	sigset_t s, o;
	struct itimerval it;
	struct sigaction sa;
	int rv;

	sigfillset(&s);
	syscall(SYS_sigprocmask, SIG_SETMASK, &s, &o);
	rv = syscall(SYS_getitimer, ITIMER_REAL, &it);
	assert(rv != -1);
#if 0
	al = syscall(SYS_alarm, 0);
	rs_log("al=%d, it=%ld.%ld\n", al,
		it.it_value.tv_sec, it.it_value.tv_usec);
	/* cannot deal with both alarm and itimer set */
	assert(!al || !(it.it_value.tv_sec || it.it_value.tv_usec));
	if (al) {
		them.tv_sec = al;
		them.tv_usec = 0;
		bzero(&themreset, sizeof(themreset));
	} else {
		them = it.it_value;
		themreset = it.it_interval;
	}
#else
	them = it.it_value;
	themreset = it.it_interval;
#endif
	resettimer();
	bzero(&sa, sizeof(sa));
	sa.sa_sigaction = sigalrm;
	sa.sa_flags = SA_SIGINFO;
	rv = syscall(SYS_sigaction, SIGALRM, &sa, &themaction);
	assert(rv != -1);
	syscall(SYS_sigprocmask, SIG_SETMASK, &o, NULL);
}

struct refuntbl {
	char *from;
	void *to;
};

struct refuntbl refuntbl[] = {
	{ "sigaction",       xsigaction },
	{ "signal",          xsignal },
	{ "sigprocmask",     xsigprocmask },
	{ "sigsuspend",      xsigsuspend },
	{ "alarm",           xalarm },
	{ "setitimer",       xsetitimer },
	{ "getitimer",       xgetitimer },
	{ NULL, NULL }
};

void
sig_init()
{
	struct refuntbl *rp;
	sigset_t s, o;
	struct sigaction sa;
	int rv;
	struct modulelist *ml;

	sigfillset(&s);
	syscall(SYS_sigprocmask, SIG_SETMASK, &s, &o);
	hijacktimer();

	bzero(&sa, sizeof(sa));
	sa.sa_sigaction = rtioready;
	sa.sa_flags = SA_SIGINFO;
	rv = syscall(SYS_sigaction, mysig, &sa, NULL);
	assert(rv != -1);
	rp = refuntbl;
	ml = rf_parse(getpid());
	while (rp->from) {
		if (0 > rf_replace_libc_function(ml, rp->from, rp->to))
			assert(0);
		rp++;
	}
	rf_free_modulelist(ml);
	syscall(SYS_sigprocmask, SIG_SETMASK, &o, NULL);
}
