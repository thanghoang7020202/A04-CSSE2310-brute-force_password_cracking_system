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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MAX_ARGS 5
#define MIN_ARGS 1
#define MAX_PORT_NUM 65535
#define MIN_PORT_NUM 1024
#define WORD_BUFFER_SIZE 51
#define MAX_WORD_LEN 8

typedef struct CmdArg {
    unsigned int maxconnections;
    char* port;
    char* dictionary;
} CmdArg;

typedef struct {
    int numWords;
    char** wordArray;
} WordList;

void exit_code_one() {
    fprintf(stderr, "Usage: crackserver [--maxconn connections]" 
            " [--port portnum] [--dictionary filename]\n");
    exit(1);
}

void exit_code_two(char* filename) {
    fprintf(stderr, "crackserver: unable to open dictionary file \"%s\"\n", filename);
    exit(2);
}

void exit_code_three() {
    fprintf(stderr, "crackserver: no plain text words to test\n");
    exit(3);
}

int contain_string(char* str) {
    for (int i = 0; i < strlen(str); i++) {
        if(!isdigit(str[i])) {
            return 1; // contain a word!
        }
    }
    return 0; // not contain any word
}

/*https://stackoverflow.com/questions/44330230/how-do-i-get-a-negative-value-by-using-atoi*/
CmdArg pre_run_checking(int argc, char* argv[]) {
    CmdArg parameters;
    parameters.maxconnections = 0;
    parameters.port = "0";
    parameters.dictionary = NULL;
    
    if (argc < MIN_ARGS || argc > MAX_ARGS || (argc % 2 == 0)) { 
        // check the number of arguments is odd or not
        // and in {1, 3, 5}
        exit_code_one();
    }
    for (int i = 1; i < argc; i++) { // check all the arguments
        if (!strcmp(argv[i], "--maxconn") 
            && parameters.maxconnections == 0) {
            i++; // get next value
            if (strstr(argv[i], "-") || contain_string(argv[i])) {
                //if not positive integer
                exit_code_one();
            }
            parameters.maxconnections = atoi(argv[i]); 
            continue;
        }
        if (!strcmp(argv[i], "--port") 
                && parameters.port == 0
                && !contain_string(argv[i+1])) {
            i++; // get next value
            unsigned int portNum = atoi(argv[i]);
            if (!(portNum > MIN_PORT_NUM && portNum < MAX_PORT_NUM) 
            && portNum != 0) {
                //if not in range
                exit_code_one();
            }
            parameters.port = argv[i];
            continue;
        }
        if (!strcmp(argv[i], "--dictionary")
                && parameters.dictionary == NULL) {
            i++; // get next value
            parameters.dictionary = argv[i];
            continue;
        }
        exit_code_one();
    }
    return parameters;
}  

// ref: from csse2310 assignment 1 solution code
WordList add_word_to_list(WordList words, char* word) {
    char* wordCopy;

    // Make a copy of the word into newly allocated memory
    wordCopy = strdup(word);

    // Make sure we have enough space to store our list (array) of
    // words.
    words.wordArray = realloc(words.wordArray,
	    sizeof(char*) * (words.numWords + 1));
    words.wordArray[words.numWords] = wordCopy;	// Add word to list
    words.numWords++;	// Update count of words
    return words;
}

// ref: from csse2310 assignment 1 solution code
WordList read_dictionary(FILE* fileStream) {
    WordList validWords;
    char currentWord[WORD_BUFFER_SIZE]; 	// Buffer to hold word.

    // Initialise our list of matches - nothing in it initially.
    validWords.numWords = 0;
    validWords.wordArray = 0;

    // Read lines of file one by one 
    while (fgets(currentWord, WORD_BUFFER_SIZE, fileStream)) {
        // Word has been read - remove any newline at the end
        // if there is one. Convert the word to uppercase.
        int wordLen = strlen(currentWord);
        if (wordLen > 0 && currentWord[wordLen - 1] == '\n') {
            currentWord[wordLen - 1] = '\0';
            wordLen--;
        }
        // If the word is longer than 8 characters, ignore it.
        if (wordLen <= MAX_WORD_LEN) {
            validWords = add_word_to_list(validWords, currentWord);
        }
    }
    if (validWords.numWords == 0) {
        exit_code_three();
    }
    return validWords;
}

// Listens on given port. Returns listening socket (or exits on failure)
int open_listen(const char* port) {
    struct addrinfo* ai = 0;
    struct addrinfo hints;

    memset(&hints, 0, sizeof(struct addrinfo)); // set of value to 0
    hints.ai_family = AF_INET;   // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    // listen on all IP addresses

    int err;
    if ((err = getaddrinfo(NULL, port, &hints, &ai))) {
        freeaddrinfo(ai);
        fprintf(stderr, "%s\n", gai_strerror(err));
        return 1;   // Could not determine address
    }

    // Create a socket and bind it to a port
    int listenfd = socket(AF_INET, SOCK_STREAM, 0); // 0=default protocol (TCP)

    // Allow address (port number) to be reused immediately
    int optVal = 1;
    if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
                &optVal, sizeof(int)) < 0) {
        perror("Error setting socket option");
        exit(1);
    }

    if(bind(listenfd, ai->ai_addr, sizeof(struct sockaddr)) < 0) {
        perror("Binding");
        return 3;
    }
    // get port number
    struct sockaddr_in ad;
    memset(&ad, 0, sizeof(struct sockaddr_in));
    socklen_t len=sizeof(struct sockaddr_in);
    if (getsockname(listenfd, (struct sockaddr*)&ad, &len)) { 
        perror("sockname");
        return 4;
    }
    printf("%u\n", ntohs(ad.sin_port)); // extract port number
    fflush(stdout);

    if(listen(listenfd, 10) < 0) {  // Up to 10 connection requests can queue
        perror("crackserver: unable to open socket for listening\n");
        return 4;
    }

    // Have listening socket - return it
    return listenfd;
}

char* capitalise(char* buffer, int len) {
    int i;

    for(i=0; i<len; i++) {
        buffer[i] = (char)toupper((int)buffer[i]);
    }
    return buffer;
}

void* client_thread(void* fdPtr) {
    int fd = *(int*)fdPtr;
    free(fdPtr);

    char buffer[1024];
    ssize_t numBytesRead;
    // Repeatedly read data arriving from client - turn it to 
    // upper case - send it back to client
    while((numBytesRead = read(fd, buffer, 1024)) > 0) {
	capitalise(buffer, numBytesRead);
	write(fd, buffer, numBytesRead);
    }
    // error or EOF - client disconnected

    if(numBytesRead < 0) {
	perror("Error reading from socket");
	exit(1);
    }
    // Print a message to server's stdout
    printf("Done with client\n");
    fflush(stdout);
    close(fd);
    return NULL;
}

void process_connections(int fdServer) {
    int fd;
    struct sockaddr_in fromAddr;
    socklen_t fromAddrSize;

    // Repeatedly accept connections and process data (capitalise)
    while(1) {
        fromAddrSize = sizeof(struct sockaddr_in);
	// Block, waiting for a new connection. (fromAddr will be populated
	// with address of client)
        fd = accept(fdServer, (struct sockaddr*)&fromAddr,  &fromAddrSize);
        if(fd < 0) {
            perror("Error accepting connection");
            exit(1);
        }
     
	// Turn our client address into a hostname and print out both 
        // the address and hostname as well as the port number
        char hostname[NI_MAXHOST];
        int error = getnameinfo((struct sockaddr*)&fromAddr, 
                fromAddrSize, hostname, NI_MAXHOST, NULL, 0, 0);
        if(error) {
            fprintf(stderr, "Error getting hostname: %s\n", 
                    gai_strerror(error));
        } else {
            printf("Accepted connection from %s (%s), port %d\n", 
                    inet_ntoa(fromAddr.sin_addr), hostname,
                    ntohs(fromAddr.sin_port));
        }

	//Send a welcome message to our client
	dprintf(fd, "Welcome\n");

	int* fdPtr = malloc(sizeof(int));
	*fdPtr = fd;

	pthread_t threadId;
	pthread_create(&threadId, NULL, client_thread, fdPtr);
	pthread_detach(threadId); // need to join later

    }
}

int main(int argc, char** argv) {
    CmdArg para = pre_run_checking(argc,argv);
    
    FILE* dictionary = fopen(para.dictionary, "r");
    if (dictionary == NULL) {
    	exit_code_two(para.dictionary);
    }
    WordList words = read_dictionary(dictionary);
    int fdServer = open_listen(para.port);
    process_connections(fdServer);
    
    fclose(dictionary);
    return 0;
}
