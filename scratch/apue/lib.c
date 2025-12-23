#include "apue.h"

/* size of control buffer to send/recv one file descriptor */
#define CONTROLLEN CMSG_LEN (sizeof (int))
static struct cmsghdr *cmptr = NULL; /* malloc'ed first time */

#define QLEN 10
#define STALE 30

#define CLI_PATH "/var/tmp/"
#define CLI_PERM S_IRWXU /* rwx for user only */

static struct termios save_termios;
static int ttysavefd = -1;
static enum { RESET, RAW, CBREAK } ttystate = RESET;

int
send_err (int fd, int errcode, const char *msg)
{
    int n;
    if ((n = strlen (msg)) > 0)
        if (writen (fd, msg, n) != n) /* send the error message */
            return (-1);
    if (errcode >= 0)
        errcode = -1; /* must be negative */
    if (send_fd (fd, errcode) < 0)
        return (-1);
    return (0);
}

ssize_t /* Read "n" bytes from a descriptor */
readn (int fd, void *ptr, size_t n)
{
    size_t nleft;
    ssize_t nread;
    nleft = n;
    while (nleft > 0) {
        if ((nread = read (fd, ptr, nleft)) < 0) {
            if (nleft == n)
                return (-1); /* error, return -1 */
            else
                break; /* error, return amount read so far */
        } else if (nread == 0) {
            break; /* EOF */
        }
        nleft -= nread;
        ptr += nread;
    }
    return (n - nleft); /* return >= 0 */
}

ssize_t /* Write "n" bytes to a descriptor */
writen (int fd, const void *ptr, size_t n)
{
    size_t nleft;
    ssize_t nwritten;
    nleft = n;
    while (nleft > 0) {
        if ((nwritten = write (fd, ptr, nleft)) < 0) {
            if (nleft == n)
                return (-1); /* error, return -1 */
            else
                break; /* error, return amount written so far */
        } else if (nwritten == 0) {
            break;
        }
        nleft -= nwritten;
        ptr += nwritten;
    }
    return (n - nleft); /* return >= 0 */
}

/*
 * Pass a file descriptor to another process.
 * If fd<0, then -fd is sent back instead as the error status.
 */
int
send_fd (int fd, int fd_to_send)
{
    struct iovec iov[1];
    struct msghdr msg;
    char buf[2]; /* send_fd()/recv_fd() 2-byte protocol */
    iov[0].iov_base = buf;
    iov[0].iov_len = 2;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    if (fd_to_send < 0) {
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        buf[1] = -fd_to_send; /* nonzero status means error */
        if (buf[1] == 0)
            buf[1] = 1; /* -256, etc. would screw up protocol */
    } else {
        if (cmptr == NULL && (cmptr = malloc (CONTROLLEN)) == NULL)
            return (-1);
        cmptr->cmsg_level = SOL_SOCKET;
        cmptr->cmsg_type = SCM_RIGHTS;
        cmptr->cmsg_len = CONTROLLEN;
        msg.msg_control = cmptr;
        msg.msg_controllen = CONTROLLEN;
        *(int *)CMSG_DATA (cmptr) = fd_to_send; /* the fd to pass */
        buf[1] = 0;                             /* zero status means OK */
    }
    buf[0] = 0; /* null byte flag to recv_fd() */
    if (sendmsg (fd, &msg, 0) != 2)
        return (-1);
    return (0);
}

int
recv_fd (int fd, ssize_t (*userfunc) (int, const void *, size_t))
{
    int newfd, nr, status;
    char *ptr;
    char buf[MAXLINE];
    struct iovec iov[1];
    struct msghdr msg;
    status = -1;
    for (;;) {
        iov[0].iov_base = buf;
        iov[0].iov_len = sizeof (buf);
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        if (cmptr == NULL && (cmptr = malloc (CONTROLLEN)) == NULL)
            return (-1);
        msg.msg_control = cmptr;
        msg.msg_controllen = CONTROLLEN;
        if ((nr = recvmsg (fd, &msg, 0)) < 0) {
            err_ret ("recvmsg error");
            return (-1);
        } else if (nr == 0) {
            err_ret ("connection closed by server");
            return (-1);
        }
        /*
         * See if this is the final data with null & status. Null
         * is next to last byte of buffer; status byte is last byte.
         * Zero status means there is a file descriptor to receive.
         */
        for (ptr = buf; ptr < &buf[nr];) {
            if (*ptr++ == 0) {
                if (ptr != &buf[nr - 1])
                    err_dump ("message format error");
                status = *ptr & 0xFF; /* prevent sign extension */
                if (status == 0) {
                    if (msg.msg_controllen != CONTROLLEN)
                        err_dump ("status = 0 but no fd");
                    newfd = *(int *)CMSG_DATA (cmptr);
                } else {
                    newfd = -status;
                }
                nr -= 2;
            }
        }
        if (nr > 0 && (*userfunc) (STDERR_FILENO, buf, nr) != nr)
            return (-1);
        if (status >= 0)    /* final data has arrived */
            return (newfd); /* descriptor, or -status */
    }
}

int
csopen (char *name, int oflag)
{
    int len;
    char buf[12];
    struct iovec iov[3];
    static int csfd = -1;
    if (csfd < 0) { /* open connection to conn server */
        if ((csfd = cli_conn (CS_OPEN)) < 0) {
            err_ret ("cli_conn error");
            return (-1);
        }
    }
    sprintf (buf, " %d", oflag);   /* oflag to ascii */
    iov[0].iov_base = CL_OPEN " "; /* string concatenation */
    iov[0].iov_len = strlen (CL_OPEN) + 1;
    iov[1].iov_base = name;
    iov[1].iov_len = strlen (name);
    iov[2].iov_base = buf;
    iov[2].iov_len = strlen (buf) + 1; /* null always sent */
    len = iov[0].iov_len + iov[1].iov_len + iov[2].iov_len;
    if (writev (csfd, &iov[0], 3) != len) {
        err_ret ("writev error");
        return (-1);
    }
    /* read back descriptor; returned errors handled by write() */
    return (recv_fd (csfd, write));
}

int
fd_pipe (int fd[2])
{
    return (socketpair (AF_UNIX, SOCK_STREAM, 0, fd));
}

int
serv_listen (const char *name)
{
    int fd, len, err, rval;
    struct sockaddr_un un;
    if (strlen (name) >= sizeof (un.sun_path)) {
        errno = ENAMETOOLONG;
        return (-1);
    }
    /* create a UNIX domain stream socket */
    if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0)
        return (-2);
    unlink (name); /* in case it already exists */
    /* fill in socket address structure */
    memset (&un, 0, sizeof (un));
    un.sun_family = AF_UNIX;
    strcpy (un.sun_path, name);
    len = offsetof (struct sockaddr_un, sun_path) + strlen (name);
    /* bind the name to the descriptor */
    if (bind (fd, (struct sockaddr *)&un, len) < 0) {
        rval = -3;
        goto errout;
    }
    if (listen (fd, QLEN) < 0) { /* tell kernel we're a server */
        rval = -4;
        goto errout;
    }
    return (fd);
errout:
    err = errno;
    close (fd);
    errno = err;
    return (rval);
}

int
serv_accept (int listenfd, uid_t *uidptr)
{
    int clifd, err, rval;
    socklen_t len;
    time_t staletime;
    struct sockaddr_un un;
    struct stat statbuf;
    char *name;
    /* allocate enough space for longest name plus terminating null */
    if ((name = malloc (sizeof (un.sun_path + 1))) == NULL)
        return (-1);
    len = sizeof (un);
    if ((clifd = accept (listenfd, (struct sockaddr *)&un, &len)) < 0) {
        free (name);
        return (-2); /* often errno=EINTR, if signal caught */
    }
    /* obtain the client's uid from its calling address */
    len -= offsetof (struct sockaddr_un, sun_path); /* len of pathname */
    memcpy (name, un.sun_path, len);
    name[len] = 0; /* null terminate */
    if (stat (name, &statbuf) < 0) {
        rval = -3;
        goto errout;
    }
#ifdef S_ISSOCK /* not defined for SVR4 */
    if (S_ISSOCK (statbuf.st_mode) == 0) {
        rval = -4; /* not a socket */
        goto errout;
    }
#endif
    if ((statbuf.st_mode & (S_IRWXG | S_IRWXO))
        || (statbuf.st_mode & S_IRWXU) != S_IRWXU) {
        rval = -5; /* is not rwx------ */
        goto errout;
    }
    staletime = time (NULL) - STALE;
    if (statbuf.st_atime < staletime || statbuf.st_ctime < staletime
        || statbuf.st_mtime < staletime) {
        rval = -6; /* i-node is too old */
        goto errout;
    }
    if (uidptr != NULL)
        *uidptr = statbuf.st_uid; /* return uid of caller */
    unlink (name);                /* we're done with pathname now */
    free (name);
    return (clifd);
errout:
    err = errno;
    close (clifd);
    free (name);
    errno = err;
    return (rval);
}

int
cli_conn (const char *name)
{
    int fd, len, err, rval;
    struct sockaddr_un un, sun;
    int do_unlink = 0;
    if (strlen (name) >= sizeof (un.sun_path)) {
        errno = ENAMETOOLONG;
        return (-1);
    }
    /* create a UNIX domain stream socket */
    if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0)
        return (-1);
    /* fill socket address structure with our address */
    memset (&un, 0, sizeof (un));
    un.sun_family = AF_UNIX;
    sprintf (un.sun_path, "%s%05ld", CLI_PATH, (long)getpid ());
    len = offsetof (struct sockaddr_un, sun_path) + strlen (un.sun_path);
    unlink (un.sun_path); /* in case it already exists */
    if (bind (fd, (struct sockaddr *)&un, len) < 0) {
        rval = -2;
        goto errout;
    }
    if (chmod (un.sun_path, CLI_PERM) < 0) {
        rval = -3;
        do_unlink = 1;
        goto errout;
    }
    /* fill socket address structure with server's address */
    memset (&sun, 0, sizeof (sun));
    sun.sun_family = AF_UNIX;
    strcpy (sun.sun_path, name);
    len = offsetof (struct sockaddr_un, sun_path) + strlen (name);
    if (connect (fd, (struct sockaddr *)&sun, len) < 0) {
        rval = -4;
        do_unlink = 1;
        goto errout;
    }
    return (fd);
errout:
    err = errno;
    close (fd);
    if (do_unlink)
        unlink (un.sun_path);
    errno = err;
    return (rval);
}

void
daemonize (const char *cmd)
{
    int i, fd0, fd1, fd2;
    pid_t pid;
    struct rlimit rl;
    struct sigaction sa;
    /*
     * Clear file creation mask.
     */
    umask (0);
    /*
     * Get maximum number of file descriptors.
     */
    if (getrlimit (RLIMIT_NOFILE, &rl) < 0)
        err_quit ("%s: can't get file limit", cmd);
    /*
     * Become a session leader to lose controlling TTY.
     */
    if ((pid = fork ()) < 0)
        err_quit ("%s: can't fork", cmd);
    else if (pid != 0) /* parent */
        exit (0);
    setsid ();
    /*
     * Ensure future opens won't allocate controlling TTYs.
     */
    sa.sa_handler = SIG_IGN;
    sigemptyset (&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction (SIGHUP, &sa, NULL) < 0)
        err_quit ("%s: can't ignore SIGHUP", cmd);
    if ((pid = fork ()) < 0)
        err_quit ("%s: can't fork", cmd);
    else if (pid != 0) /* parent */
        exit (0);
    /*
     * Change the current working directory to the root so
     * we won't prevent file systems from being unmounted.
     */
    if (chdir ("/") < 0)
        err_quit ("%s: can't change directory to /", cmd);
    /*
     * Close all open file descriptors.
     */
    if (rl.rlim_max == RLIM_INFINITY)
        rl.rlim_max = 1024;
    for (i = 0; i < rl.rlim_max; i++)
        close (i);
    /*
     * Attach file descriptors 0, 1, and 2 to /dev/null.
     */
    fd0 = open ("/dev/null", O_RDWR);
    fd1 = dup (0);
    fd2 = dup (0);
    /*
     * Initialize the log file.
     */
    openlog (cmd, LOG_CONS, LOG_DAEMON);
    if (fd0 != 0 || fd1 != 1 || fd2 != 2) {
        syslog (LOG_ERR, "unexpected file descriptors %d %d %d", fd0, fd1,
                fd2);
        exit (1);
    }
}

void
tty_atexit (void) /* can be set up by atexit(tty_atexit) */
{
    if (ttysavefd >= 0)
        tty_reset (ttysavefd);
}

int
tty_raw (int fd) /* put terminal into a raw mode */
{
    int err;
    struct termios buf;
    if (ttystate != RESET) {
        errno = EINVAL;
        return (-1);
    }
    if (tcgetattr (fd, &buf) < 0)
        return (-1);
    save_termios = buf; /* structure copy */
    /*
     * Echo off, canonical mode off, extended input
     * processing off, signal chars off.
     */
    buf.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /*
     * No SIGINT on BREAK, CR-to-NL off, input parity
     * check off, don't strip 8th bit on input, output
     * flow control off.
     */
    buf.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /*
     * Clear size bits, parity checking off.
     */
    buf.c_cflag &= ~(CSIZE | PARENB);
    /*
     * Set 8 bits/char.
     */
    buf.c_cflag |= CS8;
    /** Output processing off.
     */
    buf.c_oflag &= ~(OPOST);
    /*
     * Case B: 1 byte at a time, no timer.
     */
    buf.c_cc[VMIN] = 1;
    buf.c_cc[VTIME] = 0;
    if (tcsetattr (fd, TCSAFLUSH, &buf) < 0)
        return (-1);
    /*
     * Verify that the changes stuck. tcsetattr can return 0 on
     * partial success.
     */
    if (tcgetattr (fd, &buf) < 0) {
        err = errno;
        tcsetattr (fd, TCSAFLUSH, &save_termios);
        errno = err;
        return (-1);
    }
    if ((buf.c_lflag & (ECHO | ICANON | IEXTEN | ISIG))
        || (buf.c_iflag & (BRKINT | ICRNL | INPCK | ISTRIP | IXON))
        || (buf.c_cflag & (CSIZE | PARENB | CS8)) != CS8
        || (buf.c_oflag & OPOST) || buf.c_cc[VMIN] != 1
        || buf.c_cc[VTIME] != 0) {
        /*
         * Only some of the changes were made. Restore the
         * original settings.
         */
        tcsetattr (fd, TCSAFLUSH, &save_termios);
        errno = EINVAL;
        return (-1);
    }
    ttystate = RAW;
    ttysavefd = fd;
    return (0);
}

int
tty_reset (int fd) /* restore terminal's mode */
{
    if (ttystate == RESET)
        return (0);
    if (tcsetattr (fd, TCSAFLUSH, &save_termios) < 0)
        return (-1);
    ttystate = RESET;
    return (0);
}

Sigfunc *
signal_intr (int signo, Sigfunc *func)
{
    struct sigaction act, oact;
    act.sa_handler = func;
    sigemptyset (&act.sa_mask);
    act.sa_flags = 0;
#ifdef SA_INTERRUPT
    act.sa_flags |= SA_INTERRUPT;
#endif
    if (sigaction (signo, &act, &oact) < 0)
        return (SIG_ERR);
    return (oact.sa_handler);
}