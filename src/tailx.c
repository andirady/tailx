#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <linux/limits.h>

typedef struct {
    int wd;
    off_t offset;
    int len; 
    char *dir;
    char *path;
} target_t;

// GLOBAL STATES
int path_max = 0;
// END GLOBAL STATES

int max_path_len(int npath, char **argv)
{
    int max = 0;
    char *bname;

    for (int i = 0; i < npath; i++)
    {
        int len = strlen(argv[i]);
        if (len > max) max = len;
    }
    return max;
}

int get_new_size(int fd, off_t offset)
{
    off_t sz = lseek(fd, 0, SEEK_END);
    if (sz < 0)
        return offset;

    lseek(fd, offset, SEEK_SET);
    return sz;
}

target_t *get_target_by_wd(int ntarget, target_t *targets, int wd)
{
    for (int i = 0; i < ntarget; i++)
    {
        target_t *t = targets + i;
        if (t->wd == wd) 
            return t;
    }

    return NULL;
}

int target_with_wd(int ntarget, target_t *targets, int inotd, int wd, const char* name, void (*f)(int, target_t*))
{
    int count = 0;
    char path[PATH_MAX];
    for (int i = 0; i < ntarget; i++)
    {
        target_t *t = targets + i;
        snprintf(path, sizeof path, "%s/%s", t->dir, name);
        if (strcmp(path, t->path) == 0 && t->wd == wd)
        {
            f(inotd, t);
            count++;
        }
    }

    return count;
}

int watch_dir(int inotd, target_t *target)
{
    int wd = inotify_add_watch(inotd, target->dir, IN_CREATE | IN_DELETE);
    if (wd < 0) return -1;

    target->wd = wd;
    target->offset = 0;

    return 0;
}

void tail(target_t *t)
{
    int fd = open(t->path, O_RDONLY);
    printf("fd = %d\n", fd);
    int sz = get_new_size(fd, t->offset);
    int bufsz = sz - t->offset;
    char *buf = malloc(bufsz * sizeof(char));

    int bytes_read = read(fd, buf, bufsz);
    printf("path=%s offset=%d bytes_read=%d, sz=%d bufsz=%d\n", t->path, t->offset, bytes_read, sz, bufsz);

    char *token;
    int total = 0;
    while ((token = strsep(&buf, "\n")) != NULL)
    {
        total += strlen(token) + 1;
        //printf("total=%d, bufsz=%d, %s\n", total, bufsz, token);
        if (total <= bufsz)
            printf("%*s: %s\n", -path_max, t->path, token);
    }

    t->offset = sz;

    if (token != NULL)
        free(token);

    free(buf);
    close(fd);
}

void file_present_handler(int inotd, target_t *t)
{
    //printf("Trying to watch %s\n", t->path);
    int wd = inotify_add_watch(inotd, t->path, IN_MODIFY | IN_DELETE);
    if (wd < 0) {
        perror("File present");
        exit(EXIT_FAILURE);
    }

    t->wd = wd;
    t->offset = 0;

    //printf("Tailing... %s, %d\n", t->path, t->offset);
    tail(t);
}


int main(int argc, char **argv)
{
    char *prog = argv[0];
    int inotd = inotify_init();
    if (inotd < 0)
    {
        perror("inotify_init");
        return errno;
    }

    int npath = argc - 1;
    path_max = max_path_len(npath, argv+1);
    target_t *targets = malloc(npath * sizeof *targets);

    char *path, *dir;
    for (int i = 0; i < (argc - 1); i++)
    {
        target_t *t = targets + i;
        path = argv[i + 1];
        dir = dirname(strdup(path));

        t->len  = strlen(path);
        t->dir  = (char*)malloc(sizeof(char*));
        t->path = (char*)malloc(sizeof(char*));

        strcpy(t->dir, strdup(dir));
        strcpy(t->path, strdup(path));

        int wd = inotify_add_watch(inotd, t->path, IN_CREATE | IN_MODIFY | IN_DELETE);
        if (wd < 0)
        {
            if (errno != ENOENT)
            {
                perror("open");
                return errno;
            }

            wd = inotify_add_watch(inotd, t->dir, IN_CREATE | IN_DELETE | IN_ONLYDIR);
        }

        if (wd < 0)
        {
            perror("Failed to watch");
            return errno;
        }

        t->wd     = wd;
        t->offset = 0;
        printf("target: dir=%s, path=%s, wd=%d\n", t->dir, t->path, wd);
    }

    //printf("Starting...\n");

    char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *ev;
    char *ptr;

    for (;;)
    {
        int len = read(inotd, buf, sizeof buf);
        if (len < 0)
        {
            perror("read");
            return errno;
        }

        for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + ev->len)
        {
            ev = (const struct inotify_event*)ptr;

            //printf("New event?: wd=%d, mask=%u, cookie=%u, len=%u, name=%s\n", ev->wd, ev->mask, ev->cookie, ev->len, ev->name);
            if (ev->mask & IN_CREATE)
            {
                printf("IN_CREATE: wd=%d, name=%s\n", ev->wd, ev->name);
                if (ev->name != NULL) {
                    printf("Invoking target_with_wd");
                    target_with_wd(npath, targets, inotd, ev->wd, ev->name, file_present_handler);
                }
            }
            if (ev->mask & IN_MODIFY)
            {
                printf("IN_MODIFY\n");
                target_t *t = get_target_by_wd(npath, targets, ev->wd);
                if (t != NULL)
                    tail(t);
                else
                    printf("No target found with wd = %d\n", ev->wd);
            }
            if (ev->mask & IN_DELETE)
            {
                char is_dir = ev->mask & IN_ISDIR;
                printf("IN_DELETE: wd=%d, name=%s\n", ev->wd, ev->name);
            }
        }
    }

    //printf("Terminating\n");
    perror(prog);
    return errno;
}
