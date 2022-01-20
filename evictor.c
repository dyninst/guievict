#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>
#include <sys/un.h>
#include <unistd.h>

/* hijack.c */
int hijack(int pid, char *libname);

static void
usage()
{
	fprintf(stderr, "usage: evictor PID COMMAND [ARGS ...]\n");
	fprintf(stderr, "Valid COMMAND [ARGS ...] are:\n");
	fprintf(stderr,
		"hijack [LIBRARY]\n"
		"   (default LIBRARY is \"librt.so\" in evictor's working directory)\n");
	fprintf(stderr, "detach\n");
	fprintf(stderr, "reattach [DISPLAY]\n"
		"   (default DISPLAY is $DISPLAY of target process)\n");
	exit(1);
}

static int
sendcmd(int argc, char *argv[])
{
	int pid, fd, sk;
	char buf[1024];
	struct sockaddr_un laddr, paddr;
	char tmp[] = "/tmp/evictXXXXXX";
	int i, len;
	char *msg, *p;

	pid = atoi(argv[1]);
	fd = mkstemp(tmp);
	if (0 > fd) {
		perror("mkstemp");
		return -1;
	}
	close(fd);
	unlink(tmp);
	bzero(&laddr, sizeof(laddr));
	laddr.sun_family = AF_LOCAL;
	strncpy(laddr.sun_path, tmp, strlen(tmp));
	
	sprintf(buf, "/tmp/evict.%d.%d", pid, getuid());
	bzero(&paddr, sizeof(paddr));
	paddr.sun_family = AF_LOCAL;
	strncpy(paddr.sun_path, buf, strlen(buf));

	len = 0;
	for (i = 2; i < argc; i++)
		len += strlen(argv[i]);
	/* pack all args separated
	   by spaces into one string*/
	msg = malloc(len+argc-1);
	if (!msg) {
		fprintf(stderr, "out of memory\n");
		return -1;
	}
	p = msg;
	for (i = 2; i < argc; i++) {
		if (i > 2)
			*p++ = ' ';
		memcpy(p, argv[i], strlen(argv[i]));
		p += strlen(argv[i]);
	}
	*p = '\0';

	sk = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (0 > bind(sk, (struct sockaddr *)&laddr, SUN_LEN(&laddr))) {
		perror("bind");
		return -1;
	}
	if (0 > connect(sk, (struct sockaddr *)&paddr, SUN_LEN(&paddr))) {
		perror("connect");
		return -1;
	}

	if (0 > sendto(sk, msg, strlen(msg), 0, (struct sockaddr *)&paddr,
		       SUN_LEN(&paddr))) {
		perror("sendto");
		return -1;
	}
	return 0;
}

static int
resolvelibname(char *lib, char *buf, int bufsz)
{
	char *def = "librt.so";

	if (!lib)
		lib = def;
	if (lib[0] != '/') {
		/* absolute path in the current directory */
		if (!getcwd(buf, bufsz)
		    || (strlen(buf)+1+strlen(lib) >= bufsz)) {
			fprintf(stderr, "limit exceeded\n");
			return -1;
		}
		strcat(buf, "/");
		strcat(buf, lib);
	} else if (strlen(lib) >= bufsz) {
		fprintf(stderr, "limited exceeded\n");
		return -1;
	} else
		strcpy(buf, lib);
	return access(buf, R_OK|X_OK);
}

int
main(int argc, char *argv[])
{
	int rv, pid;
	char libname[1024];

	if (argc < 3)
		usage();

	pid = atoi(argv[1]);
	if (0 > kill(pid, 0)) {
		fprintf(stderr, "Cannot control process %d: %s\n",
			pid, strerror(errno));
		exit(1);
	}

	if (!strcasecmp("hijack", argv[2])) {
		if (0 > resolvelibname(argc > 3 ? argv[3] : NULL,
				       libname, sizeof(libname))) {
			fprintf(stderr, "cannot determine library name\n");
			exit(1);
		}
		rv = hijack(pid, libname);
	}
	else
		rv = sendcmd(argc, argv);

	if (!rv)
		exit(0);
	else
		exit(1);
}
