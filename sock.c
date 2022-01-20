#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <assert.h>
#include <errno.h>
#include <sys/un.h>
#include <netinet/ip.h>
#include <sys/socket.h>

#include "rt.h"

int
findsock()
{
	DIR *dir;
	struct dirent *e;
	int rv, pid;
	char buf[1024];
	char s[1024];
	unsigned long inode, i;
	int sock, sk, len;
	struct sockaddr *sa;
	struct sockaddr_un *sau;
	struct sockaddr_in *sai;

	pid = getpid();
	snprintf(s, sizeof(s), "/proc/%d/fd", pid);
	dir = opendir(s);
	if (!dir)
		return -1;
	sock = inode = -1;
	while ((e = readdir(dir))) {
		snprintf(s, sizeof(s), "/proc/%d/fd/%s", pid, e->d_name);
		rv = readlink(s, buf, sizeof(buf));
		if (0 > rv)
			continue;
		if (rv >= sizeof(buf)) {
			rs_log("buffer overflow\n");
			continue;
		}
		buf[rv] = '\0';
		rv = sscanf(buf, "socket:[%lu]", &i);
		if (rv != 1)
			continue; /* not a socket */
		sk = atoi(e->d_name);
		sa = (struct sockaddr *)s;
		len = sizeof(s);
		rv = getpeername(sk, sa, &len);
		if (0 > rv) {
			perror("getpeername");
			continue;
		}
		switch (sa->sa_family) {
		case AF_INET:
			sai = (struct sockaddr_in *)sa;
			if (sai->sin_port < htons(6000)
			    || sai->sin_port >= htons(6010))
				continue;
			break;
		case AF_UNIX:
			sau = (struct sockaddr_un *)sa;
			if (!strstr(sau->sun_path, "X11"))
				continue;
			break;
		default:
			continue;
		}
		if (sock >= 0 && inode != i) {
			rs_log("WARNING: multiple X connections!\n");
			continue;
		}
		sock = sk;
		inode = i;
	}
	return sock;
}
