#include <errno.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "postgres.h"

#include "utils/elog.h"
#include "invalid.h"
#include "wal_reader.h"


#define MAX_EVENTS 100
#define LEN_NAME 25
#define EVENT_SIZE  (sizeof (struct inotify_event))
#define BUF_LEN     (MAX_EVENTS * (EVENT_SIZE + LEN_NAME))

#define XLOGDIR	"pg_wal"


int wd;
int wal_reader_fd;

static int init_notifier(void) {
    wal_reader_fd = inotify_init1(0);
    if (wal_reader_fd == -1) {
        char* emsg = strerror(errno);
        ereport(ERROR, errmsg("init_wal_reader: can't inotify_init1: %s", emsg));
        abort();
    }

    wd = inotify_add_watch(wal_reader_fd, XLOGDIR, IN_MODIFY);
    if (wd == -1) {
        char* emsg = strerror(errno);
        ereport(ERROR, errmsg("init_wal_reader: can't inotify_add_watch: %s", emsg));
        abort();
    }
    return wal_reader_fd;
}

static void finish_nofier (void) {
    inotify_rm_watch(wal_reader_fd, wd);
    close(wal_reader_fd);
}

static void process_record(XLogRecord*  record, XLogReaderState* xlogreader) {

}

static void* start_invalidator(void* arg) {
    char buffer[BUF_LEN];
    int wal_reader_fd = init_notifier();
    wal_info* wal = init_wal_info();

    while (1) {
        int length = read(wal_reader_fd, buffer, BUF_LEN );
        int cur_index = 0;

        while (cur_index < length) {
            struct inotify_event* event = (struct inotify_event*) &(buffer[cur_index]);
            if (event->len && event->mask == IN_MODIFY) {
                XLogRecord*  record = read_next_XLog_record(wal);

            }
            cur_index += EVENT_SIZE + event->len;
        }
    }
    finish_nofier();
    destroy_wal_info(wal);
}

void init_invalidator(void) {
    pthread_t wr_tid;
    int err = pthread_create(&(wr_tid), NULL, start_invalidator, NULL);
    if (err) {
        ereport(INFO, errmsg("init_cache_gc: pthread_create error %s", strerror(err)));
        abort();
    }
}
