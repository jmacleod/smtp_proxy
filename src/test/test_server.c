#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
 
#define PORT "3492"  // the port users will be connecting to
#define MAXDATASIZE 100 // max number of bytes we can get at once 
#define BACKLOG 10     // how many pending connections queue will hold
#define NUM_SMTP_LISTENER_THREADS 10 
pthread_mutex_t thread_ready_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  thread_ready_var[NUM_SMTP_LISTENER_THREADS];

void initialize_thread_ready_var (){
    int i;
    for (i=0;i<NUM_SMTP_LISTENER_THREADS;i++){
        thread_ready_var[i]   = PTHREAD_COND_INITIALIZER;
    }
}
int fds[NUM_SMTP_LISTENER_THREADS];

struct ready_thread_struct {
   int num;
   struct ready_thread_struct * next;
};
typedef struct ready_thread_struct ready_thread_struct;
ready_thread_struct ready_threads = NULL;

 
void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}
 
// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
 
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
 
int mark_self_as_ready(){
    //get same mutex as get_next_ready_thread
    //how does this thread know its number?
    //will block and wait for cond var, not in here, but after this function returns
    //need a pointer to the end of the list, so we don;t need to traverse to add threeads to end of the list
    //malloc a new struct
    //how does a thread know its number?  howeber that is set num to this thread
    //set next to null
    //update last pointer to point to this new cell
    //if initial list pointer is null, point it to this
    //point last entries  next to this new struct, it should have been null, if not then error
    //release mutex

}

int get_next_ready_thread() {
    int thread_num;
    //need to mutex this
    if (ready_threads == NULL){
        //maybe wait for some time before and try again befoer returning failure, would need to release mutex so threads could be add to list
        return -1;
    }
    thread_num = ready_threads->num;
    ready_threads = ready_threads->next;
    //delete this ready_thread so we dont leak memory, we have the num, and have updated the pointer
    //release mutex
    return(thread_num);
} 

void *listener (){
    int sockfd, new_fd;  // listen on sock_fd 
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;
 
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
 
    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) == -1) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(0) ;
    }
 
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            continue;
        }
 
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }
 
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
 
        break;
    }
 
    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(0);
    }
 
    freeaddrinfo(servinfo); // all done with this structure
 
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }
 
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
 
    printf("server: waiting for connections...\n");
 
    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }
 
        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s, sizeof s);
        printf("server: got connection from %s\n", s);

        pthread_mutex_lock( &thread_ready_mutex );
        if (ready_thread_num = get_next_ready_thread()==-1){ // get a ready thread
            perror("need more threads");
        }
        fds[ready_thread_num] = new_fd; // update ready thread's fd with new one from accept
        // signal that thread
        pthread_cond_signal( thread_ready_var[ready_thread_num] );
        pthread_mutex_unlock( &thread_ready_mutex );
    }
} 

void *handler(){
    int numbytes = 0;
    char buf[MAXDATASIZE];

    while (1){
    // Lock mutex and then wait for signal to relase mutex
    pthread_mutex_lock( &thread_ready_mutex );
    //update self as ready
    // mutex unlocked if condition var is set 
    pthread_cond_wait( &thread_ready_var[0], &thread_ready_mutex );
    if (send(new_fd, "220 mx.changeme.com ESMTP\n", 26, 0) == -1){

        perror("send");
    }

    if ((numbytes = recv(new_fd, buf, MAXDATASIZE-1, 0)) == -1) {
        perror("recv");
        exit(1);
    }

    buf[numbytes] = '\0';

    printf("got:  '%s'\n",buf);

    close(new_fd);
    pthread_mutex_unlock( &thread_ready_mutex );

  }

}
 
int main(void)
{
    initialize_thread_ready_var();
    pthread_t listener_thread, handler_thread;
	 
    pthread_create( &listener_thread, NULL, &listener, NULL);
    pthread_create( &handler_thread, NULL, &handler, NULL);
    
    pthread_join( listener_thread, NULL);
    pthread_join( handler_thread, NULL);
    return 0;
}
 
 
 
 
