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
//#define MAXDATASIZE 100 // max number of bytes we can get at once 
#define BACKLOG 10     // how many pending connections queue will hold
pthread_mutex_t count_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  condition_var   = PTHREAD_COND_INITIALIZER;
int new_fd;
 
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
 
void *listener (){
    int sockfd;  // listen on sock_fd 
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

        pthread_mutex_lock( &count_mutex );
        pthread_cond_signal( &condition_var );
        pthread_mutex_unlock( &count_mutex );
    }
} 

void *handler(){
    while (1){
    // Lock mutex and then wait for signal to relase mutex
    pthread_mutex_lock( &count_mutex );

    // mutex unlocked if condition varialbe in functionCount2() signaled.
    pthread_cond_wait( &condition_var, &count_mutex );
    if (send(new_fd, "Hello, world!", 13, 0) == -1){
        perror("send");
    }
    close(new_fd);
    pthread_mutex_unlock( &count_mutex );

  }

}
 
int main(void)
{
   pthread_t listener_thread, handler_thread;
	 
    pthread_create( &listener_thread, NULL, &listener, NULL);
    pthread_create( &handler_thread, NULL, &handler, NULL);
    
    pthread_join( listener_thread, NULL);
    pthread_join( handler_thread, NULL);
    return 0;
}
 
 
 
 
