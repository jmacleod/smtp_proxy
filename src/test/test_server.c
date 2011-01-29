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
pthread_mutex_t thread_ready_mutex     = PTHREAD_MUTEX_INITIALIZER; //do we maybe need an arry of these for each thread? and a seperate mutex in the fuctions for updating?
pthread_cond_t  thread_ready_var[NUM_SMTP_LISTENER_THREADS];

void initialize_thread_ready_var (){
    int i;
    for (i=0;i<NUM_SMTP_LISTENER_THREADS;i++){
        pthread_cond_init(&thread_ready_var[i],NULL);
        //thread_ready_var[i]   = PTHREAD_COND_INITIALIZER;
    }
}
int fds[NUM_SMTP_LISTENER_THREADS];

//preallocate because we know how many we only need as many as there are threads, that way we don't need to malloc for linked list
struct  ready_thread_struct {
   int thread_num;
   struct ready_thread_struct * next;
} ready_thread_struct_array[NUM_SMTP_LISTENER_THREADS];
struct ready_thread_struct *ready_threads = NULL, *last_ready_thread = NULL;

void initialize_ready_thread_struct(){
    int i;
    for (i=0;i<NUM_SMTP_LISTENER_THREADS;i++){
        ready_thread_struct_array[i].thread_num=i;
        ready_thread_struct_array[i].next=NULL;
    }
}
    
 
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
 
int mark_self_as_ready(int thread_num){  //maybe this can be the initial state or action in state machine 

    // Lock mutex and then wait for signal to relase mutex
    pthread_mutex_lock( &thread_ready_mutex );
    //update self as ready clear 
    ready_thread_struct_array[thread_num].next = NULL;
    if (ready_threads->next == NULL){
        //if the list is empty make this the first node.
        ready_threads->next = &ready_thread_struct_array[thread_num];
    }else {
        //elese insert this at the end
        last_ready_thread->next = &ready_thread_struct_array[thread_num];
    }
    last_ready_thread = &ready_thread_struct_array[thread_num];
    // mutex unlocked if condition var is set 
    pthread_cond_wait( &thread_ready_var[thread_num], &thread_ready_mutex );
}

int get_next_ready_thread() {
    int thread_num;
    
    pthread_mutex_lock( &thread_ready_mutex );

    if (ready_threads == NULL){
        //maybe wait for some time before and try again before returning failure, would need to release mutex so threads could be add to list
        return -1;
    }
    thread_num = ready_threads->thread_num;
    ready_threads = ready_threads->next;

    pthread_mutex_unlock( &thread_ready_mutex );
    return(thread_num);
} 

void *listener (){
    int sockfd, new_fd;  // listen on sock_fd, and on accept get a new_fd
    int ready_thread_num;
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
        pthread_cond_signal( &thread_ready_var[ready_thread_num] );
        pthread_mutex_unlock( &thread_ready_mutex );
    }
} 

void *worker(int *thread_num_in){
    int thread_num = *thread_num_in;
    int numbytes = 0;
    char buf[MAXDATASIZE];
    printf("entering worker thread_num(%d)\n",thread_num);
    while (1){
        mark_self_as_ready(thread_num);

        sprintf(buf,"220 mx.changeme.com ESMTP(thread id: %d\n",thread_num);
        if (send(fds[thread_num], buf, sizeof(buf), 0) == -1){
            perror("send");
        }

        if ((numbytes = recv(fds[thread_num], buf, MAXDATASIZE-1, 0)) == -1) {
            perror("recv");
            exit(1);
        }

        buf[numbytes] = '\0';

        printf("got:  '%s'\n",buf);

        close(fds[thread_num]);
        pthread_mutex_unlock( &thread_ready_mutex );
  }

}
 
int main(void)
{
    pthread_t listener_thread;
    pthread_t worker_threads[NUM_SMTP_LISTENER_THREADS];
    int i;

printf("1\n");
    initialize_thread_ready_var();
printf("2\n");
    initialize_ready_thread_struct();
	 
printf("3\n");
    for (i=0;i<NUM_SMTP_LISTENER_THREADS;i++){
        printf("3 %d\n",i);
        pthread_create( &worker_threads[i], NULL, &worker, &i);
    }

printf("4\n");
    pthread_create( &listener_thread, NULL, &listener, NULL);

printf("5\n");
    
    pthread_join( listener_thread, NULL);

    for (i=0;i<NUM_SMTP_LISTENER_THREADS;i++){
        pthread_join( worker_threads[i], NULL);
    }
    return 0;
}
 
 
 
 
