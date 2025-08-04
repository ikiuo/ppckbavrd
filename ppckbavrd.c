#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef FALSE
#define FALSE (!!0)
#endif
#ifndef TRUE
#define TRUE  (!0)
#endif

/*
 *
 */

#ifndef NAME
#define NAME  "ppckbavrd"
#endif
#ifndef VERSION
#define VERSION  "0.1.0"
#endif

#ifndef DEFAULT_DEVICE
#define DEFAULT_DEVICE  "/dev/ttyS1"
#endif
#ifndef DEFAULT_DIRECTORY
#define DEFAULT_DIRECTORY  "/etc/ppckbavrd"
#endif
#ifndef DEFAULT_SHELL
#define DEFAULT_SHELL  "/bin/sh"
#endif
#ifndef DEFAULT_PIDFILE
#define DEFAULT_PIDFILE  "/var/run/" NAME ".pid"
#endif

/*
 *
 */

typedef unsigned long long msec_t;
typedef struct termios termios_t;
typedef struct timeval timeval_t;
typedef struct stat stat_t;

/*
 *
 */

static const char default_device[] = DEFAULT_DEVICE;
static const char default_directory[] = DEFAULT_DIRECTORY;
static const char default_pidfile[] = DEFAULT_PIDFILE;
static const char default_shell[] = DEFAULT_SHELL;

static const char *device = default_device;
static const char *directory = default_directory;
static const char *pidfile = default_pidfile;

static char **environ = NULL;
static const char *program = NULL;
static int flag_daemon = TRUE;
static int flag_verbose = FALSE;

static int avr_fd = -1;
static int last_code = -1;
static msec_t last_table[256];

/*
 *
 */

static void XSYSLOG(int priority, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsyslog(priority, fmt, ap);
    va_end(ap);

    if (!flag_daemon)
    {
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        putc('\n', stderr);
        va_end(ap);
    }
}

#define SYSLOG_ERR(...)  XSYSLOG(LOG_ERR, __VA_ARGS__)
#define SYSLOG_INFO(...)  XSYSLOG(LOG_INFO, __VA_ARGS__)
#define SYSLOG_DEBUG(...)  XSYSLOG(LOG_DEBUG, __VA_ARGS__)
#define VERBOSE(...) { if (flag_verbose) SYSLOG_DEBUG(__VA_ARGS__); }

/*
 *
 */

static struct {
    int sig;
    const char *name;
} const signal_table[] = {
#define SIG_NAME(n) { n, #n }
    SIG_NAME(SIGHUP),
    SIG_NAME(SIGINT),
    SIG_NAME(SIGQUIT),
    SIG_NAME(SIGABRT),
    SIG_NAME(SIGKILL),
    SIG_NAME(SIGTERM),
    { 0, NULL },
#undef SIG_NAME
};

static const char *signal_name(int sig)
{
    const char *name;
    int i;

    for (i = 0; (name = signal_table[i].name); ++i)
        if (sig == signal_table[i].sig)
            break;
    return name;
}

static void signal_handler(int sig)
{
    const char *name = signal_name(sig);

    if (name)
        SYSLOG_INFO("signal: %s", name);
    else
        SYSLOG_INFO("signal: %d", sig);

    close(avr_fd);
    avr_fd = -1;
}

static void set_signal_handler()
{
    int i;

    for (i = 0; (signal_table[i].name); ++i)
        signal(signal_table[i].sig, signal_handler);
}

/*
 *
 */

static msec_t get_msec()
{
    timeval_t tv;

    if (!gettimeofday(&tv, NULL))
        return (msec_t) tv.tv_sec * 1000 + tv.tv_usec / 1000;
    return (msec_t) time(NULL) * 1000;
}

static void delete_pidfile(const char *path)
{
    unlink(path);
}

static int make_pidfile(const char *path, pid_t pid)
{
    FILE *fp;
    int wrote = FALSE;

    if (!(fp = fopen(path, "w")))
    {
        SYSLOG_ERR("can't open: %s", path);
    }
    else
    {
        wrote = fprintf(fp, "%d", pid) > 0;
        fclose(fp);

        if (!wrote)
        {
            SYSLOG_ERR("write error: %s", path);
            delete_pidfile(path);
        }
    }
    return wrote;
}

static int check_exec(const char *path)
{
    stat_t fst;

    if (!stat(path, &fst))
        return S_ISREG(fst.st_mode) && (fst.st_mode & S_IXUSR);
    VERBOSE("  not executable: %s", path);
    return FALSE;
}

static void subprocess(char **argv)
{
    pid_t pid;
    int cstat;

    pid = vfork();
    if (pid < 0)
    {
        SYSLOG_ERR("vfork() failed: errno=%d", errno);
        return;
    }
    if (pid > 0)
    {
        waitpid(pid, &cstat, 0);
        return;
    }

    SYSLOG_INFO("  exec: %s", argv[2]);
    execve(argv[0], argv, environ);
    exit(2);
}

static void command(const char *fmt, ...)
{
    va_list ap;
    char *argv[4];
    char cmdline[256];
    size_t cmdsize = sizeof(cmdline) - 1;

    argv[0] = (char*) default_shell;
    argv[1] = "-c";
    argv[2] = cmdline;
    argv[3] = NULL;

    cmdline[cmdsize] = 0;

    va_start(ap, fmt);
    vsnprintf(cmdline, cmdsize, fmt, ap);
    va_end(ap);

    subprocess(argv);
}

static int daemon_main(const char *device, const char *directory)
{
    static char hex[] = "0123456789abcdef";
    static char sseq[] = "RTZ";
    static char ccmd[] = "event-00";
    static char acmd[] = "allevent";

    termios_t tca;

    int count;
    int res;
    int ncode, pcode, scode;
    msec_t dsec, nsec, psec;

    fd_set rfds;
    char rwbuf[16];
    int i, c;

    if (chdir(directory) < 0)
    {
        SYSLOG_ERR("chdir failed: \"%s\"", directory);
        return 2;
    }
    VERBOSE("chdir: \"%s\"", directory);

    avr_fd = open(device, O_RDWR | O_NOCTTY);
    if (avr_fd < 0)
    {
        SYSLOG_ERR("open failed: \"%s\"", device);
        return 2;
    }
    VERBOSE("open: \"%s\"", device);

    memset(&tca, 0, sizeof(tca));
    tca.c_iflag = INPCK;
    tca.c_oflag = OPOST;
    tca.c_cflag = B9600 | CS8 | CSTOPB | CREAD | PARENB | CLOCAL;
    /* tca.c_lflag = 0; */
    res = tcsetattr(avr_fd, TCSANOW, &tca);
    if (res < 0)
    {
        SYSLOG_ERR("tcsetattr failed: \"%s\"", device);
        close(avr_fd);
        return 2;
    }

    for (i = 0; (c = sseq[i]); i++)
    {
        memset(rwbuf, c, 4);
        res = write(avr_fd, rwbuf, 4);
        if (res != 4)
            SYSLOG_ERR("write(...) error: %d", res);
        else
            VERBOSE("write: code=%#04x(%c)", c, c);
    }

    for (count = 0; avr_fd >= 0;)
    {
        FD_ZERO(&rfds);
        FD_SET(avr_fd, &rfds);

        res = select(avr_fd + 1, &rfds, NULL, NULL, NULL);
        if (res < 0)
        {
            SYSLOG_ERR("select(...) error: retry=%d", count);
            goto retry;
        }
        if (!res || !FD_ISSET(avr_fd, &rfds))
        {
            VERBOSE("select(...): empty");
            goto next;
        }

        nsec = get_msec();
        res = read(avr_fd, rwbuf, 1);
        if (res < 0)
        {
            SYSLOG_ERR("read(...) error: retry=%d", count);
            goto retry;
        }

        count = 0;
        if (!res)
        {
            VERBOSE("read(...): empty");
            goto next;
        }

        ncode = rwbuf[0] & 0xff;
        pcode = last_code;
        last_code = ncode;
        scode = pcode == ncode;

        psec = last_table[ncode];
        last_table[ncode] = nsec;
        dsec = psec ? nsec - psec : 0;

        switch (ncode)
        {
        case 0x20:
        case 0x22:
            if (scode && dsec < 2000)
                continue;
            psec = last_table[ncode + 1];
            dsec = psec ? nsec - psec : 0;
            break;

        default:
            break;
        }


        nsec /= 1000;
        psec /= 1000;
        dsec /= 1000;

        SYSLOG_INFO("event[%llu]: code=%#04x", nsec, ncode);

        ccmd[6] = hex[(ncode >> 4) & 0x0f];
        ccmd[7] = hex[(ncode >> 0) & 0x0f];

        if (check_exec(acmd))
            command("./%s %s %02x %llu %llu", acmd, device, ncode, nsec, dsec);
        if (check_exec(ccmd))
            command("./%s %s %02x %llu %llu", ccmd, device, ncode, nsec, dsec);
        goto next;

    retry:
        if (++count >= 50)
          {
            SYSLOG_ERR("give up: \"%s\"", device);
            break;
          }
    next:
        usleep(10 * 1000); /* 10ms */
    }

    close(avr_fd);
    return 2;
}

/*
 *
 */

static int usage()
{
    printf(
        "AVR monitoring daemon for KURO-BOX HD/HG Version " VERSION ".\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "options are:\n"
        "    -h        this help.\n"
        "    -v        set verbose mode.\n"
        "    -N        set non-daemon mode.\n"
        "    -P DEV    set the AVR device file. (default: %s)\n"
        "    -S DIR    set the script dirctory. (default: %s)\n"
        "    --pidfile PATH\n"
        "              set the pid file path. (default: %s)\n"
        "\n"
        , program
        , default_device
        , default_directory
        , default_pidfile
        );
    return 1;
}

static char *shift_arg(int *argc, char ***argv)
{
    char *arg = NULL;

    if ((*argc) > 0)
    {
        arg = *(*argv);
        --(*argc);
        ++(*argv);
    }
    return arg;
}

#define shift()  (shift_arg(&argc, &argv))

int main (int argc, char **argv, char **envp)
{
    int new_argc = 0;
    char **new_argv = argv;
    char *arg;
    int s_opt;

    int exitcode = 0;
    int use_pid = FALSE;

    environ = envp;
    program = shift();

    while (argc > 0)
    {
        arg = shift();
        if ((arg[0] != '-') || (arg[1] == 0))
        {
            new_argv[new_argc++] = arg;
            continue;
        }
        if (arg[1] == '-')
        {
            if (!strcmp(arg, "--pidfile"))
            {
                if (!(pidfile = shift()))
                    return usage();
                continue;
            }
            return usage();
        }
        while ((s_opt = *(++arg)))
        {
            switch (s_opt)
            {
            case 'h':
                return usage();

            case 'P':
                if (!arg[1] && (device = shift()))
                    continue;
                break;

            case 'S':
                if (!arg[1] && (directory = shift()))
                    continue;
                break;

            case 'N':
                flag_daemon = FALSE;
                continue;

            case 'v':
                flag_verbose = TRUE;
                continue;

            default:
                break;
            }
            return usage ();
        }
    }

    argc = new_argc;
    argv = new_argv;

    if (argc)
        return usage();
    if (flag_daemon && daemon(0, 0))
    {
        perror("???");
        return 2;
    }

    openlog(NAME, LOG_PID, LOG_USER);

    set_signal_handler();
    use_pid = make_pidfile(pidfile, getpid());
    exitcode = daemon_main(device, directory);
    if (use_pid)
        delete_pidfile(pidfile);
    closelog();

    return exitcode;
}
