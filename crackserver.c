#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <csse2310a3.h>
#include <csse2310a4.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <crypt.h>
#include <semaphore.h>

#define MAX_ARGS 7
#define MIN_ARGS 1
#define MAX_PORT_NUM 65535
#define MIN_PORT_NUM 1024
#define MAX_CONNECTIONS 50
#define MIN_CONNECTIONS 1
#define WORD_BUFFER_SIZE 51
#define MAX_WORD_LEN 8
#define CIPHERTEXT_LEN 13

//sem_t sem;

typedef struct CmdArg {
    unsigned int maxconnections;
    char* port;
    char* dictionary;
} CmdArg;

typedef struct {
    int numWords;
    char** wordArray;
} WordList;

typedef struct {
    int fd;
    FILE* toClient;
    WordList words;
} Client;

typedef struct {
    char* salt;
    char* password;
    WordList words;
    int start; // range in dictionary
    int end;
    char* result;
} Resource;

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

void exit_code_four() {
    fprintf(stderr, "crackserver: unable to open socket for listening\n");
    exit(4);
}

int valid_salt(char* salt) {
    if (strlen(salt) != 2) {
        return 0; // 0 means invalid
    }
    int flag = 1; // 1 means valid
    for (int i = 0; i < strlen(salt); i++) {
        if (!(isalnum(salt[i]) || salt[i] == '.' || salt[i] == '/')) {
            flag = 0; // switch to invalid
            break;
        }
    }
    return flag;
}

char* valid_plain_text(char* plainText) {
    if (strlen(plainText) > 8) {
        //return a first 8 characters
        char* validText = malloc(sizeof(char)*9); 
        strncpy(plainText, validText, 8);
        validText[8] = '\0'; // add end of string
        return validText;
    }
    return plainText;
}

int valid_integer(char* connection) {
    for (int i = 0; i < strlen(connection); i++) {
        if(!isdigit(connection[i])) {
            return 0; // 0 means invalid
        }
    }
    return 1; // 1 means valid
}

CmdArg re_update_parameter(CmdArg parameters) {
    if (parameters.port == NULL) {
        parameters.port = "0";
    }
    if (parameters.dictionary == NULL) {
        parameters.dictionary = "/usr/share/dict/words";
    }
    if (parameters.maxconnections == -1) {
        parameters.maxconnections = INT_MAX;
    }
    return parameters;
} 

/*https://stackoverflow.com/questions/44330230/how-do-i-get-a-negative-value-by-using-atoi*/
CmdArg pre_run_checking(int argc, char* argv[]) {
    CmdArg parameters;
    parameters.maxconnections = -1; // -1 means not set yet
    parameters.port = NULL;
    parameters.dictionary = NULL;
    
    if (argc < MIN_ARGS || argc > MAX_ARGS || (argc % 2 == 0)) { 
        // check the number of arguments is odd or not
        // and in {1, 3, 5}
        exit_code_one();
    }
    for (int i = 1; i < argc; i++) { // check all the arguments
        if (!strcmp(argv[i], "--maxconn") 
            && parameters.maxconnections == -1) {
            i++; // get next value
            if (strstr(argv[i], "-")
                    || !valid_integer(argv[i])) {
                //if not positive integer
                exit_code_one();
            }
            parameters.maxconnections = atoi(argv[i]); 
            continue;
        }
        if (!strcmp(argv[i], "--port") 
                && parameters.port == NULL
                && valid_integer(argv[i+1])) {
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
    // if null or missing -> set default value
    parameters = re_update_parameter(parameters);
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
    char* currentWord = NULL; 	// Buffer to hold word.

    // Initialise our list of matches - nothing in it initially.
    validWords.numWords = 0;
    validWords.wordArray = 0;

    // Read lines of file one by one 
    while ((currentWord = read_line(fileStream)) != NULL) {
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
int open_listen(const char* port, int maxconnections) {
    struct addrinfo* ai = 0;
    struct addrinfo hints;

    memset(&hints, 0, sizeof(struct addrinfo)); // set of value to 0
    hints.ai_family = AF_INET;   // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    // listen on all IP addresses

    if ((getaddrinfo(NULL, port, &hints, &ai))) {
        freeaddrinfo(ai);
        exit_code_four();// Could not determine address
    }

    // Create a socket and bind it to a port
    int listenfd = socket(AF_INET, SOCK_STREAM, 0); // 0=default protocol (TCP)

    // Allow address (port number) to be reused immediately
    int optVal = 1;
    if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
                &optVal, sizeof(int)) < 0) {
        close(listenfd);
        exit_code_four(); //Error setting socket option
    }

    if(bind(listenfd, ai->ai_addr, sizeof(struct sockaddr)) < 0) {
        close(listenfd);
        exit_code_four(); // Could not bind to address
    }
    // get port number
    struct sockaddr_in ad;
    memset(&ad, 0, sizeof(struct sockaddr_in));
    socklen_t len=sizeof(struct sockaddr_in);
    if (getsockname(listenfd, (struct sockaddr*)&ad, &len)) {
        close(listenfd);
        exit_code_four(); // getsockname failed
    }
    // extract port number to stderr
    fprintf(stderr,"%u\n", ntohs(ad.sin_port));
    fflush(stderr);

    if(listen(listenfd, maxconnections) < 0) {  // Up to 10 connection requests can queue
        close(listenfd);
        exit_code_four(); // Could not listen on socket
    }

    // Have listening socket - return it
    return listenfd;
}

char* salt_extract(char** token) {
    // token[0] = "crack"
    // token[1] = ciphertext
    // token[2] = maxconnections
    char* salt = malloc(sizeof(char)*2);
    if (strlen(token[1]) != CIPHERTEXT_LEN 
            || !valid_integer(token[2])
            || atoll(token[2]) < MIN_CONNECTIONS 
            || atoll(token[2]) > MAX_CONNECTIONS) {
        free(salt);
        return ":invalid\n";
    }
    strncpy(salt, token[1], 2);
    if (!valid_salt(salt)) {
        free(salt);
        return ":invalid\n";
    }
    return salt;
}

//ref: https://stackoverflow.com/questions/9335777/crypt-r-example
void* finder(void* resource) {
    Resource* res = (Resource*)resource;
    //sem_wait(&sem);
    for (int i = res->start; i <= res->end; i++) {
        struct crypt_data data;
        data.initialized = 0;
        char* libPass = crypt_r(res->words.wordArray[i], res->salt, &data);
        //fprintf(stdout,"(%s) == (%s : %s)\n", res->password, libPass, res->words.wordArray[i]);
        //fflush(stdout);
        if (strcmp(res->password, libPass) == 0) {
            char* result = strdup(res->words.wordArray[i]);
            result = strcat(result, "\n");
            res->result = malloc(sizeof(char)*strlen(result));
            res->result = result;
            return NULL;
        }
    }
    //sem_post(&sem);
    res->result = ":failed\n";
    return NULL;  
}

char* brute_force_cracker(char* password, char* salt, WordList words, char* nthread) {
    if (strcmp(salt, ":invalid\n") == 0) {
        return ":invalid\n";
    }
    //create resource    
    int numThread = 
        (nthread == NULL || words.numWords < atoll(nthread))? 1: atoll(nthread);
    int perThread = (int) (words.numWords / numThread); // round down
    int remainder = words.numWords % numThread;
    Resource resource[numThread];
    //create thread
    pthread_t* threadId = malloc(sizeof(pthread_t)*numThread);

    for(int i = 0; i < numThread; i++) {
        //initialize starting and ending index
        resource[i].salt = salt;
        resource[i].words = words;
        resource[i].password = password;
        resource[i].start = i * perThread;
        resource[i].end = ((i+1) * perThread) - 1;
        if (i == numThread - 1) {
            resource[i].end += remainder;
        }
        pthread_create(&threadId[i], NULL, finder, &resource[i]);
    }
    //join thread
    for (int i = 0; i < numThread; i++) {
        pthread_join(threadId[i], NULL);
        if (strcmp(resource[i].result, ":failed\n") != 0) {
            for(int j = i + 1; j < numThread; j++) {
                if (i != j) {
                    pthread_detach(threadId[j]);
                }
            }
            return resource[i].result;
        }
    }
    return ":failed\n";
}

char* instructionAnalysis(char* buffer, WordList* words) {
    //replace \n at the end with end of string
    buffer[strlen(buffer)-1] = '\0';
    if(buffer[0] == ' '|| buffer[strlen(buffer)-1] == ' ') {
        return ":invalid\n";
    }
    int tokenCount = 0;
    char** token = split_space_not_quote(buffer, &tokenCount);
    if (tokenCount != 3) {
        return ":invalid\n"; // empty string mean error
    }
    //fprintf(stdout,"token[0] = %s\n token[1] = %s\n token[2] = %s", token[0], token[1], token[2]);
    //fflush(stdout);
    if (strcmp(token[0], "crack") == 0) {
        char* salt = salt_extract(token);
        buffer = brute_force_cracker(token[1], salt, *words, token[2]);
    } else if (strcmp(token[0], "crypt") == 0) {
        if (!valid_salt(token[2])) {
            //fprintf(stdout,"invalid instruction 2\n");
            //fflush(stdout);
            return ":invalid\n";
        }
        //get 8 first characters of plain text if it is longer than 8
        token[2] = valid_plain_text(token[2]);
        *words = add_word_to_list(*words, token[1]);
        struct crypt_data data;
        data.initialized = 0;
        buffer = strdup(crypt_r(token[1], token[2], &data));
        buffer = strcat(buffer, "\n");
    } else {
        return ":invalid\n"; // empty string mean error
    }
    //add \n at the end of string
    free(token);
    return buffer;
}

void* client_thread(void* ClientPtr) {
    Client client = *(Client*)ClientPtr;
    free(ClientPtr);

    char* buffer = malloc(sizeof(char)*1024);
    ssize_t numBytesRead;
    // Repeatedly read data arriving from client - turn it to 
    // upper case - send it back to client
    for(; (numBytesRead = read(client.fd, buffer, 1024)) > 0;) {
        //capitalise(buffer, numBytesRead);
        char* result = instructionAnalysis(buffer, &(client.words));
        write(client.fd, result, strlen(result)*sizeof(char));
        if (result != NULL 
                && strcmp(result, ":failed\n") != 0
                && strcmp(result, ":invalid\n") != 0) {
            free(result);
        }
        free(buffer);
        buffer = malloc(sizeof(char)*1024);
    }
    // error or EOF - client disconnected

    if(numBytesRead < 0) {
        perror("Error reading from socket");
        exit(1);
    }
    // Print a message to server's stdout
    close(client.fd);
    return NULL;
}

void process_connections(int fdServer, WordList words) {
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
            exit_code_four();
        }

    Client* client = malloc(sizeof(Client));
    client->fd = fd;
    int fd2 = dup(fd);
    client->toClient = fdopen(fd2, "w");
    close(fd2);
    client->words = words;

	pthread_t threadId;
	pthread_create(&threadId, NULL, client_thread, client);
	pthread_detach(threadId); // need to join later
    fclose(client->toClient);
    }
}

int main(int argc, char** argv) {
    CmdArg para = pre_run_checking(argc,argv);
    
    //open dictionary to read
    FILE* dictionary = fopen(para.dictionary, "r");
    if (dictionary == NULL) {
    	exit_code_two(para.dictionary);
    }
    //array contains valid words
    WordList words = read_dictionary(dictionary);
    fclose(dictionary);

    int fdServer = open_listen(para.port, para.maxconnections);
    //sem_init(&sem, 0,1);
    process_connections(fdServer, words);
    free(words.wordArray);
    //sem_destroy(&sem);
    return 0;
}
