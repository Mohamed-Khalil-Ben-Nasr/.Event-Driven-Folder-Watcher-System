#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <sys/epoll.h>

//custom struct
struct file
{
    char path;
    char tag;
    int wd;
};

// Global flag
int running;

//create signal handler function
void sig_handler(int sig)
{
  running = 0;
}

int main(int argc, char *argv[])
{
    pid_t child_pid = fork();

    if (child_pid == 0)
    {
        running = 1;
  
        // Install signal handler
        signal(SIGINT, sig_handler);
        signal(SIGTERM, sig_handler);

        // Open log file
        FILE *log_f = fopen("fwd3.log", "r");

        // Read config file
        FILE *config_f = fopen("fwd3.conf","r");

        //figure out the number of directories in the config file
        int dir_count = 0;
        char buffer[100];
        while( fgets(buffer, sizeof(buffer), config_f) != NULL){
            dir_count ++;
        }
        rewind(config_f);
        printf("%d\n",dir_count);
        //intialize inotify
        int inotify_fd = inotify_init1 (0);

        if (inotify_fd == -1) {
        perror ("inotify_init1");
        exit (EXIT_FAILURE);
        }

        //make an array of structs
        struct file f[dir_count];

        // set up watches
        for(int n = 0; n < dir_count; n++) {
            //read path and tag from single line from config file: basically take care of one file 
            //char s[100];
            //fgets(s, sizeof(s), config_f);

            // extract tag and path from config file line
            char tag[64];
            char path[256];
            fscanf(config_f, "%s %s", tag, path);
            printf("Trying to watch %s\n",path);
            // add watcher
            int wd;
            wd = inotify_add_watch (inotify_fd, path, IN_CREATE | IN_MODIFY);
            if (wd == -1) {
            perror ("inotify_add_watch");
            exit (EXIT_FAILURE);
            }

            for (int i; i< dir_count; i++){
                struct file curf;
                strncpy(&curf.tag, tag, sizeof(curf.tag));
                strncpy(&curf.path, path, sizeof(curf.path));
                curf.wd = wd;
                f[i] = curf;
            }
        }
        
        printf("about to create epoll");
        struct epoll_event *events;
        events = malloc(sizeof(struct epoll_event) * dir_count);

        // create epoll instance
        int epfd = epoll_create1 (0);

        while(running) {
            
            for (int i=0; i<dir_count; i++){

                //create epoll event
                struct epoll_event inotifyEvent;
                inotifyEvent.events = EPOLLIN; // Events to monitor for inotify
                inotifyEvent.data.fd = inotify_fd; // inotify file descriptor

                //add file descriptor to epoll instance
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, inotify_fd, &inotifyEvent) == -1) {
                    perror("epoll_ctl (inotifyFd)");
                    exit(1);
                }

                // add inotify event to array of events
                events[i] = inotifyEvent;
            }


            // epoll wait
            int nr_events = epoll_wait (epfd, events, dir_count, -1);

            // Block signals
            sigset_t set;
            sigemptyset(&set);
            sigaddset(&set, SIGINT);
            sigaddset(&set, SIGTERM);
            sigprocmask(SIG_BLOCK, &set, NULL);

            // read inotify events
            printf("about to read");
            struct inotify_event evts[3];
            int rd = read(inotify_fd,evts,3*sizeof(struct inotify_event));

            printf("events read");

            // Process events
            for (int i = 0; i < nr_events; i++) {
                struct inotify_event target = evts[i];
                //loop through the list of directories 
                for (int j = 0; j < dir_count; j++){
                    //if i find a directory that was changed apply these changes                   
                    if (f[j].wd == target.wd ){
            
                        // note current time
                        time_t cur_time;
                        time(&cur_time);

                        fprintf(log_f, "%s ", target.name);
                        fprintf(log_f, "%s\n", ctime(&cur_time));
                        fflush(config_f);
                    }
                }
            }
            free(events);

            // Unblock signals
            sigprocmask(SIG_UNBLOCK, &set, NULL);
        }

        fclose(log_f);
        fclose(config_f);
       
    }
    else
    {
        FILE *pid_f = fopen("/run/fwd.pid", "w");
        fprintf(pid_f, "%d", child_pid);
        fclose(pid_f);
    }

}





