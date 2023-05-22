// CSSE2310 - Week 10 - net2.c - client
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <csse2310a3.h>
#include <csse2310a4.h> 

#define MAX_ARGS 3
#define MIN_ARGS 2

typedef struct argv {
    char* port;
    int fdJobfile;
} argv_t;

void exit_status_one() {
    fprintf(stderr, "Usage: crackclient portnum [jobfile]\n");
    exit(1);
}

void exit_status_two(char* jobfile) {
    fprintf(stderr, "crackclient: unable to open job file \"%s\"\n", jobfile);
    exit(2);
}

void exit_status_three(const char* port) {
    fprintf(stderr,"crackclient: unable to connect to port %s\n", port);
    exit(3);
}

void exit_status_four() {
    fprintf(stderr, "crackclient: server connection terminated\n");
    exit(4);
}

argv_t argument_checker(int argc, char** argv){
    argv_t args;
    args.port = argv[1];
    args.fdJobfile = -1;
    // there are at least 2 arguments (./crackclient portnum) 
    // and at most 3 arguments (./crackclient portnum jobfile)
    if (argc < MIN_ARGS || argc > MAX_ARGS) { 
        exit_status_one();
    }
    if (argc == 3) {
        stdin = freopen(argv[2], "r", stdin);
        
        if (stdin == NULL) {
            //error in opening jobfile
            exit_status_two(argv[2]);
        }
        //args.fdJobfile = fdjobfile;
    }
    return args;
}

int write_to_server(FILE* to) {
    char buffer[80];
    for (;1;) {
        if (fgets(buffer, 79, stdin) != NULL) {
            if (buffer[0] == '#' || buffer[0] == 0 || buffer[0] == '\n') {
                continue; // run again
            }
            fprintf(to, "%s", buffer);
            fflush(to);
            return 0; // run again
        }
        return 1;
    }
    // break the loop (close the connection)
}

int read_from_server(FILE* from) {
    char buffer[90];
    int ifEoF = 0;
    if(fgets(buffer, 89, from) == NULL) {
            ifEoF = 1;
            return ifEoF;
    };
    if (strcmp(buffer, ":invalid\n") == 0) {
        printf("Error in command\n");
        fflush(stdout);
        return ifEoF;
    }
    if (strcmp(buffer, ":failed\n") == 0) {
        printf("Unable to decrypt\n");
        fflush(stdout);
        return ifEoF;
    }
    printf("%s", buffer);
    fflush(stdout);
    return ifEoF;
}

int main(int argc, char** argv) {
    argv_t args = argument_checker(argc,argv);

    struct addrinfo* ai = 0;
    struct addrinfo hints;
    memset(& hints, 0, sizeof(struct addrinfo));
    hints.ai_family=AF_INET;        // IPv4, for generic could use AF_UNSPEC
    hints.ai_socktype=SOCK_STREAM;
    int err;
    if ((err=getaddrinfo("localhost", args.port, &hints, &ai))) {
        freeaddrinfo(ai);
        fprintf(stderr, "%s\n", gai_strerror(err));
        return 1;   // could not work out the address
    }

    int fd=socket(AF_INET, SOCK_STREAM, 0); // 0 == use default protocol
    if (connect(fd, ai->ai_addr, sizeof(struct sockaddr))) {
        exit_status_three(args.port);
    }
    // fd is now connected
    // we want separate streams (which we can close independently)

    int fd2=dup(fd);
    FILE* to=fdopen(fd, "w");
    FILE* from=fdopen(fd2, "r");

    if (argc == MAX_ARGS) {
        //dup2(args.fdJobfile, STDIN_FILENO);
        
        //close(args.fdJobfile);
    }

    for (; 1;) {
        //sent
        if (write_to_server(to) == 1) {
            break;
        }
        // received
        if (read_from_server(from)) {
            exit_status_four();
        }
    }
    fclose(to); // close socket
    fclose(from);
    return 0;
}
