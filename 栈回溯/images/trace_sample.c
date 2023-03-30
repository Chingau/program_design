#include <signal.h>
#include <execinfo.h>

void dump(int signo)  
{  
    #define MAX_DEPTH       (20)
    #define MARK_FMT1       "\n[%010u %02u:%02u:%02u.%03u]:\n"
    #define MARK_FMT2       "\n[%04u-%02hhu-%02hhu %02hhu:%02hhu:%02hhu.%03hu]:\n"
    #define LOG_PATH        "/usrdata/stack.log"
    
    void * info[MAX_DEPTH] = {0};
    char timestr[128]      = {0};
    int  fd = 0, depth = 0;

    depth = backtrace(info, MAX_DEPTH);

    if((fd = open(LOG_PATH, O_CREAT|O_WRONLY|O_APPEND)) > 0) 
    {
        rtctm_t time;
        if (rtc_gettime(&time) < 0) {
            uint64_t tick = time_msec64();
            unsigned day, hour, min, sec, msec;
            day  = tick / 86400000;
            hour = tick % 86400000 / 3600000;
            min  = tick % 3600000 / 60000;
            sec  = tick % 60000 / 1000;
            msec = tick % 1000;
            sprintf(timestr, MARK_FMT1, day, hour, min, sec, msec);
        }
        else sprintf(timestr, MARK_FMT2, time.year + 2000, time.month, time.day,
        time.hour, time.minute, time.second, time.msec);

        write(fd, (const void*)timestr, strlen(timestr));
        backtrace_symbols_fd(info, depth, fd);
        sync();
        close(fd);
    }

    char ** strs = backtrace_symbols(info, depth);
    printf("%s",timestr);
    for (int i=0; i<depth; i++) printf("%s\n",strs[i]);
    if(strs) free(strs);
    exit(0);  
}

int main(void)
{
    if (signal(SIGSEGV, dump) == SIG_ERR)  
        perror("can't catch SIGSEGV");

    init();
    pm_loop(0);
    while(1) sleep(0xFFFFFFFF);
}

