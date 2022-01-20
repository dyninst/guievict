#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* c = a - b */
void
tv_diff(const struct timeval *a,
	const struct timeval *b,
	struct timeval *c)
{
	c->tv_sec = a->tv_sec - b->tv_sec;
	c->tv_usec = a->tv_usec - b->tv_usec;
	if (c->tv_usec < 0) {
		c->tv_sec -= 1;
		c->tv_usec += 1000000;
	}
}

void * 
xmalloc(size_t size)
{
	void *p;
	p = malloc(size);
	if (!p) {
		fprintf(stderr, "Out of memory\n");
		exit(1);
	}
	memset(p, 0, size);
	return p;
}

void *
xrealloc(void *p, size_t size)
{
	p = realloc(p, size);
	if (!p) {
		fprintf(stderr, "Out of memory\n");
		assert(0);
	}
	return p;
}

char *
xstrdup(char *str)
{
	char *p;
	p = strdup(str);
	if (!p) {
		fprintf(stderr, "Out of memory\n");
		assert(0);
	}
	return p;
}

int
xwrite(int sd, const void *buf, size_t len)
{
	char *p = (char *)buf;
	size_t nsent = 0;
	ssize_t rv;
	
	assert(len > 0); 	/* a common bug */
	while (nsent < len) {
		rv = write(sd, p, len - nsent);
		if (0 > rv && (errno == EINTR || errno == EAGAIN))
			continue;
		if (0 > rv)
			return -1;
		nsent += rv;
		p += rv;
	}
	return nsent;
}

int
xread(int sd, void *buf, size_t len)
{
	char *p = (char *)buf;
	size_t nrecv = 0;
	ssize_t rv;
	int flags;

	assert(len > 0); 	/* a common bug */
	flags = fcntl(sd, F_GETFL);
	assert(flags != -1);
	rv = fcntl(sd, F_SETFL, flags & (~O_NONBLOCK));
	assert(rv != -1);
	while (nrecv < len) {
		rv = read(sd, p, len - nrecv);
		if (0 > rv && errno == EINTR)
			continue;
		if (0 > rv)
			return -1;
		if (0 == rv)
			return 0;
		nrecv += rv;
		p += rv;
	}
	rv = fcntl(sd, F_SETFL, flags);
	assert(rv != -1);
	return nrecv;
}

/* padding needed after len bytes */
int
xpad(int len)
{
	return len%4 ? 4-len%4 : 0;
}

void
xreadpad(int sd, size_t len)
{
	int rv;
	char pad[3];

	if (!xpad(len))
		return;
	rv = xread(sd, pad, xpad(len));
	if (0 >= rv)
		assert(0);
}

int
xwritepad(int sd, const void *buf, size_t len)
{
	int rv;
	char pad[3];

	rv = xwrite(sd, buf, len);
	if (0 >= rv)
		return rv;
	if (xpad(len) > 0)
		return xwrite(sd, pad, xpad(len));
	else
		return rv;
}

int
parse_addr(const char *s, struct in_addr *addr)
{
	struct hostent* h;
	h = gethostbyname(s);
	if (!h)
		return -1;
	*addr = *((struct in_addr *) h->h_addr); /* network order */
	return 0;
}
