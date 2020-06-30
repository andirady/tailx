#include<errno.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/stat.h>
#include<sys/inotify.h>
#include<libgen.h>
#include<unistd.h>
#include<fcntl.h>

//#define DEBUG

#ifdef DEBUG
  //#define CHECK_UNIQUE
  #define debug(...) printf(__VA_ARGS__)
#else
  #define debug(...) /* */
#endif

#define die(...) { fprintf(stderr, __VA_ARGS__); exit(EXIT_FAILURE); }

typedef struct {
    int wd;
    char *name;
    off_t offset;
} target_t;

/**
 * Returns 0 on success and 1 if the file does not exists yet.
 * The program will terminate if the file as well as the parent directory
 * of the file does not exits.
 */
int watch_file(int, char *, target_t *);

/**
 * Returns the new offset
 */
size_t tail(char *, off_t);

int main(int argc, char **argv)
{
    char *prog_name = argv[0];
    char **filenames = argv+1;
    char n_files = argc - 1;

    target_t *targets;
    if ((targets = malloc(n_files * sizeof *targets)) == NULL)
        die("Not enough memory");

    int inotd = inotify_init();

    for (int i = 0; i < n_files; i++) {
        target_t *t = targets + i;
        int rc = watch_file(inotd, *(filenames + i), t);
        if (rc == 0) {
            t->offset = tail(t->name, 0);
        }
    }

    char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *ev;
    char *ptr;

    while (1) {
        int len = read(inotd, buf, sizeof buf);
        if (len == -1)
            die("Can't read events");

        for (
            ptr = buf;
            ptr < buf + len; ptr += sizeof(struct inotify_event) + ev->len
        ) {
            ev = (const struct inotify_event*)ptr;
            char *name = strndup(ev->name, ev->len);

            if (ev->mask & IN_CREATE) {
                debug("IN_CREATE: wd=%d, name='%s'\n", ev->wd, ev->len, name);
                for (int i = 0; i < n_files; i++) {
                    target_t *t = targets + i;
                    if (t->wd == ev->wd) {
                        int rc = watch_file(inotd, t->name, t);
                        if (rc == 0) {
                            t->offset = tail(t->name, 0);
                        }
                    }
                }
            } else if (ev->mask & IN_MODIFY) {
                debug("IN_MODIFY: wd=%d, len=%d, name='%s'\n", ev->wd, ev->len, name);
                for (int i = 0; i < n_files; i++) {
                    target_t *t = targets + i;
                    if (t->wd == ev->wd) {
                        t->offset = tail(t->name, t->offset);
                        break;
                    }
                }
            } else if (ev->mask & IN_DELETE) {
                debug("IN_DELETE: wd=%d, name='%s'\n", ev->wd, ev->len, name);
            } else if (ev->mask & IN_DELETE_SELF) {
                for (int i = 0; i < n_files; i++) {
                    target_t *t = targets + i;
                    if (t->wd == ev->wd) {
                        watch_file(inotd, t->name, t);
                        break;
                    }
                }
            } else {
                debug("UNKNOWN: wd=%d, len=%d, name='%s', mask=%d, cookie=%d\n",
                      ev->wd, ev->len, name, ev->mask, ev->cookie);

                if (ev->mask & IN_IGNORED)       debug("  IN_IGNORED\n");
                if (ev->mask & IN_ISDIR)         debug("  IN_ISDIR\n");
                if (ev->mask & IN_Q_OVERFLOW)    debug("  IN_Q_OVERFLOW\n");
                if (ev->mask & IN_UNMOUNT)       debug("  IN_UNMOUNT\n");
                if (ev->mask & IN_ACCESS)        debug("  IN_ACCESS\n");
                if (ev->mask & IN_ATTRIB)        debug("  IN_ATTRIB\n");
                if (ev->mask & IN_CLOSE_WRITE)   debug("  IN_CLOSE_WRITE\n");
                if (ev->mask & IN_CLOSE_NOWRITE) debug("  IN_CLOSE_NOWRITE\n");
                if (ev->mask & IN_MOVED_FROM)    debug("  IN_MOVED_FROM\n");
                if (ev->mask & IN_MOVED_TO)      debug("  IN_MOVED_TO\n");
                if (ev->mask & IN_OPEN)          debug("  IN_OPEN\n");
            }

            free(name);
        }
    }

    close(inotd);
    return 0;
}

int watch_file(int inotd, char *fn, target_t *t)
{
    int wd, rc = 0;
    struct stat st;

    t->wd = -1;
    t->name = fn; // use same reference from the main function

    if (stat(fn, &st)  == -1) {
        if (errno != ENOENT)
            die("Can't stat file: %s: %s\n", fn, strerror(errno));

        errno = 0;

        char *dir = dirname(strdup(fn));
                         // ^^^ pass copy, dirname will mutate the first arg
        if (stat(dir, &st) == -1)
            die("Can't stat file or directory: %s: %s\n", fn, strerror(errno));

        if ((wd = inotify_add_watch(inotd, dir, IN_CREATE | IN_DELETE)) == -1)
            die("Can't watch directory: %s: %s", dir, strerror(errno));

        rc = 1;
    } else if ((wd = inotify_add_watch(inotd, fn, IN_MODIFY | IN_DELETE_SELF)) == -1)
        die("Can't watch file: %s: %s", fn, strerror(errno));

    t->wd = wd;
    debug("fn=%s, wd=%d, not_exists=%d\n", fn, wd, rc);
    return rc;
}

size_t tail(char *fn, off_t offset)
{
    const char line_sep[2] = "\n";
    struct stat st;

    if (stat(fn, &st) == -1)
        die("Can't stat file: %s: %s\n", fn, strerror(errno));

    int fd = open(fn, O_RDONLY);
    if (fd == -1)
        die("Can't open file for reading: %s: %s", fn, strerror(errno));

    int rc = lseek(fd, offset, SEEK_SET);
    if (rc == -1)
        die("Can't seek file: %s: %s", fn, strerror(errno));

    char buf[BUFSIZ];
    int rd, total_rd = 0;

    int rem = st.st_size;
    int cont = 0;

    while (rem > 0) {
        int rd = read(fd, buf, BUFSIZ);
        if (rd == -1)
            die("Can't read file: %s: %s", fn, strerror(errno));

        rem -= rd;

        char *tmp = (char*)malloc((rd + 1) * sizeof(char));
        strncpy(tmp, buf, rd + 1);

        int i = 0;
#ifdef CHECK_UNIQUE
        char *prev_tok = (char*)malloc(sizeof(char*));
#endif
        char *tok = strtok(tmp, line_sep);

        while (tok != NULL) {
            char last_ch = buf[i + strlen(tok)];
            i += strlen(tok) + 1;

#ifdef CHECK_UNIQUE
            if (prev_tok != NULL && strcmp(tok, prev_tok) == 0) {
                die("Non unique line found!");
            }
            prev_tok = tok;
#endif

            if (cont) {
                printf(tok);
                cont = 0;
            } else {
                printf("%s: %s", fn, tok);
            }

            tok = strtok(NULL, line_sep);
            if (tok == NULL && rem == 0) {
                printf("\n");
                break;
            }

            if (tok != NULL || last_ch == '\n' || rem == 0) {
                printf("\n");
            } else if (rem != 0) {
                cont = 1;
            }
        }

        free(tmp);
        memset(buf, '\0', rd);

        if (!cont)
            break;
    }

    close(fd);
    debug("Done tailing %s\n", fn);
    return st.st_size;
}
