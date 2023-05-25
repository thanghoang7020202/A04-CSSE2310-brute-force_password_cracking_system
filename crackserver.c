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

#define MAX_ARGS 7
#define MIN_ARGS 1
#define MAX_PORT_NUM 65535
#define MIN_PORT_NUM 1024
#define MAX_CONNECTIONS 50
#define MIN_CONNECTIONS 1
#define WORD_BUFFER_SIZE 51
#define MAX_WORD_LEN 8
#define CIPHERTEXT_LEN 13
#define PLAINTEXT_LEN 8

/*
 * struct CmdArg is used to store the information of command line arguments
 * including the maximum number of connections, the port number and the
 * dictionary file name.
 */
typedef struct CmdArg {
    unsigned int maxconnections;
    char* port;
    char* dictionary;
} CmdArg;

/*
 * struct WordList is used to store the information of a word list
 * including the number of words and the array of words.
 */
typedef struct {
    int numWords;
    char** wordArray;
} WordList;

/*
 * struct Client is used to store the information of a client
 * including the file descriptor, the file pointer and the word list
 * which be used to pass to the client thread function.
 */
typedef struct {
    int fd;
    FILE* clientFile;
    WordList words;
} Client;

/*
 * The resource struct is used to pass information
 * to the sub-threads (worker) to run the crack function
 * on the dictionary from index start to end.
 */
typedef struct {
    char* salt;
    char* password; // plain text
    WordList words;
    int start; // range in dictionary
    int end;
    char* result;
} Resource;

/*
 * exit_code_one()
 * -----------------------
 * Prints an error message and exits the program with exit code 1.
 */
void exit_code_one() {
    fprintf(stderr, "Usage: crackserver [--maxconn connections]" 
            " [--port portnum] [--dictionary filename]\n");
    exit(1);
}

/*
 * exit_code_two()
 * -----------------------
 * Prints an error message and exits the program with exit code 2.
 */
void exit_code_two(char* filename) {
    fprintf(stderr, "crackserver: unable to open dictionary file"
            " \"%s\"\n", filename);
    exit(2);
}

/*
 * exit_code_three()
 * -----------------------
 * Prints an error message and exits the program with exit code 3.
 */
void exit_code_three() {
    fprintf(stderr, "crackserver: no plain text words to test\n");
    exit(3);
}

/*
 * exit_code_four()
 * -----------------------
 * Prints an error message and exits the program with exit code 4.
 */
void exit_code_four() {
    fprintf(stderr, "crackserver: unable to open socket for listening\n");
    exit(4);
}

/*
 * valid_port()
 * -----------------------
 * Checks the string is a valid port number or not.
 * 
 * salt: a string to be checked
 * 
 * Returns: 1 if the string is a valid port number, 0 otherwise
 */
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

/*
 * valid_plain_text()
 * -----------------------
 * Checks the string is a valid plain text or not.
 * 
 * plainText: a string to be checked
 * 
 * Returns: a valid plain text
 */
char* valid_plain_text(char* plainText) {
    if (strlen(plainText) > 8) {
        //return a first 8 characters
        char* validText = malloc(sizeof(char) * 9); 
        strncpy(plainText, validText, 8);
        validText[8] = '\0'; // add end of string
        return validText;
    }
    return plainText;
}

/*
 * valid_integer()
 * -----------------------
 * Checks the string is a valid integer or not.
 * 
 * connection: a string to be checked
 * 
 * Returns: 1 if the string is a valid integer, 0 otherwise
 */
int valid_integer(char* connection) {
    for (int i = 0; i < strlen(connection); i++) {
        if (!isdigit(connection[i])) {
            return 0; // 0 means invalid
        }
    }
    return 1; // 1 means valid
}

/*
 * re_update_parameter()
 * -----------------------
 * Checks the command line arguments for validity. If the arguments
 * are valid, the function returns a CmdArg structure containing the
 * values of the arguments.
 * 
 * parameters: a CmdArg structure containing the values of the arguments
 * (look at the definition of CmdArg)
 * 
 * Returns: a CmdArg structure containing the values of the arguments
 */
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

/*
 * pre_run_checking()
 * -----------------------
 * Checks the command line arguments for validity. If the arguments
 * are valid, the function returns a CmdArg structure containing the
 * values of the arguments. 
 * 
 * argc: the number of command line arguments
 * argv: the array of command line arguments
 * 
 * Returns: a CmdArg structure containing the values of the arguments
 * 
 * Errors: If the arguments are invalid, the function prints an error message
 *  and exits the program with exit
 * code 1.
 * 
 * REF: https://stackoverflow.com/questions/44330230/
 *      how-do-i-get-a-negative-value-by-using-atoi
 */
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

/*
 * add_word_to_list()
 * -----------------------
 * Adds the given word to the given list of words. The list of words
 * is stored in a WordList structure. (See the comment above the
 * WordList type)
 * 
 * words: The list of words to add the word to
 * word: The word to add to the list
 * 
 * returns: The updated list of words
 * 
 * REF: csse2310 assignment 1 solution code
 */
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

/*
 * read_dictionary()
 * -----------------------
 * Reads a dictionary from the given file stream, and returns a list
 * of words in the dictionary. If the dictionary file contains a word 
 * longer than 8 characters, it will be ignored. 
 * 
 * fileStream: A file stream to read the dictionary from.
 * 
 * Returns: A WordList structure containing the words in the dictionary.
 *          (See the comment above the WordList type)
 * Errors:  If the dictionary file cannot be opened, the program will
 *         print an error message to stderr and exit with exit code 3.
 *  
 * REF: from csse2310 assignment 1 solution code
 */
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

/*
 * open_listen()
 * --------------------
 * Open a listening socket on the specified port. Returns the file
 * descriptor of the listening socket.
 * 
 * port: The port number to listen on.
 * maxconnections: The maximum number of connections to accept.
 * 
 * Returns: The file descriptor of the listening socket.
 */
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
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
            &optVal, sizeof(int)) < 0) {
        close(listenfd);
        exit_code_four(); //Error setting socket option
    }

    if (bind(listenfd, ai->ai_addr, sizeof(struct sockaddr)) < 0) {
        close(listenfd);
        exit_code_four(); // Could not bind to address
    }
    // get port number
    struct sockaddr_in ad;
    memset(&ad, 0, sizeof(struct sockaddr_in));
    socklen_t len = sizeof(struct sockaddr_in);
    if (getsockname(listenfd, (struct sockaddr*)&ad, &len)) {
        close(listenfd);
        exit_code_four(); // getsockname failed
    }
    // extract port number to stderr
    fprintf(stderr, "%u\n", ntohs(ad.sin_port));
    fflush(stderr);
    if (listen(listenfd, maxconnections) < 0) { // listen for connections
        close(listenfd);
        exit_code_four(); // Could not listen on socket
    }

    // Have listening socket - return it
    return listenfd;
}

/*
 * salt_extractor()
 * ----------------------------------
 * This function is used to extract the salt from the ciphertext.
 * If the salt is invalid, it will return ":invalid\n".
 * 
 * token: the tokenized string
 * 
 * return: the salt
 */
char* salt_extractor(char** token) {
    // token[0] = "crack"
    // token[1] = ciphertext
    // token[2] = maxconnections
    char* salt = malloc(sizeof(char) * 2);
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

/*
 * finder()
 * ----------------------------------
 * This function is used to find the password from the dictionary.
 * If the password is found, it will return the password, otherwise
 * it will return ":failed\n".
 * 
 * resource: the struct that contains the information needed to find
 * the password. (look at the struct Resource)
 * 
 * Returns: the password or ":failed\n"
 * ref: https://stackoverflow.com/questions/9335777/crypt-r-example
 */
void* finder(void* resource) {
    Resource* res = (Resource*)resource;
    for (int i = res->start; i <= res->end; i++) {
        struct crypt_data data;
        data.initialized = 0;
        char* libPass = crypt_r(res->words.wordArray[i], res->salt, &data);
        if (strcmp(res->password, libPass) == 0) {
            char* result = strdup(res->words.wordArray[i]);
            result = strcat(result, "\n");
            res->result = malloc(sizeof(char) * strlen(result));
            res->result = result;
            return NULL;
        }
    }
    res->result = ":failed\n";
    return NULL;  
}

/*
 * brute_force_cracker()
 * ----------------------------------
 * This function is used to crack the password by brute force.
 * 
 * password: the password to be cracked
 * salt: the salt of the password
 * words: the wordlist (dictionary)
 * nthread: the number of threads to be used
 * 
 * return: the cracked password
 */
char* brute_force_cracker(char* password, 
        char* salt, WordList words, char* nthread) {
    if (strcmp(salt, ":invalid\n") == 0) {
        return ":invalid\n";
    }
    //create resource    
    int numThread = 
            (nthread == NULL 
            || words.numWords < atoll(nthread))? 1: atoll(nthread);
    int perThread = (int)(words.numWords / numThread); // round down
    int remainder = words.numWords % numThread;
    Resource resource[numThread];
    //create thread
    pthread_t* threadId = malloc(sizeof(pthread_t) * numThread);

    for (int i = 0; i < numThread; i++) {
        //initialize starting and ending index
        resource[i].salt = salt;
        resource[i].words = words;
        resource[i].password = password;
        resource[i].start = i * perThread;
        resource[i].end = ((i + 1) * perThread) - 1;
        if (i == numThread - 1) {
            resource[i].end += remainder;
        }
        pthread_create(&threadId[i], NULL, finder, &resource[i]);
    }
    //join thread
    for (int i = 0; i < numThread; i++) {
        pthread_join(threadId[i], NULL);
        if (strcmp(resource[i].result, ":failed\n") != 0) {
            for (int j = i + 1; j < numThread; j++) {
                if (i != j) {
                    pthread_detach(threadId[j]);
                }
            }
            return resource[i].result;
        }
    }
    return ":failed\n";
}

/*
 * instruction_analyst()
 * --------------------------------------
 * This function will analyze the instruction and call the corresponding
 * function to execute the instruction.
 * 
 * buffer: the instruction to be analyzed
 * words: the wordlist (dictionary)
 * 
 * Returns: the result of the instruction in from of a string
 * with \n at the end
 */
char* instruction_analyst(char* buffer, WordList* words) {
    //replace \n at the end with end of string
    if (buffer != NULL && !strcmp(buffer, "")) {
        buffer[strlen(buffer) - 1] = '\0';
    }
    if (buffer[0] == ' ' || buffer[strlen(buffer) - 1] == ' ') {
        return ":invalid\n";
    }
    int tokenCount = 0;
    char** token = split_space_not_quote(buffer, &tokenCount);
    if (tokenCount != 3) {
        return ":invalid\n"; // empty string mean error
    }
    if (strcmp(token[0], "crack") == 0) {
        char* salt = salt_extractor(token);
        buffer = brute_force_cracker(token[1], salt, *words, token[2]);
    } else if (strcmp(token[0], "crypt") == 0) {
        if (!valid_salt(token[2])) {
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

/*
 * client_thread()
 * --------------------------------------
 * Thread function that handles all communication with a client once
 * a connection has been established.  This function should repeatedly
 * read a line of input (plain text) from the client and crypt 
 * or crack the pain text.
 * 
 * args: clientPtr - a pointer to a Client struct containing the client's
 *                  file descriptor and the word list.
 * Returns: NULL (this return value is ignored)
 */
void* client_thread(void* clientPtr) {
    Client client = *(Client*)clientPtr;
    free(clientPtr);

    char* buffer;
    // Repeatedly read data arriving from client - turn it to 
    // upper case - send it back to client
    for (; (buffer = read_line(client.clientFile));) {
        char* result = instruction_analyst(buffer, &(client.words));
        fputs(result, client.clientFile);
        fflush(client.clientFile); // flush to client
        if (result != NULL 
                && strcmp(result, ":failed\n") != 0
                && strcmp(result, ":invalid\n") != 0) {
            free(result);
        }
        free(buffer);
    }
    close(client.fd);
    return NULL;
}

/*
 * process_connections()
 * --------------------------------------
 * Process incoming connections on the given server socket.  This function
 * will block until a connection is received.  Once a connection is received,
 * a new thread will be created to process the connection and this function
 * will go back to waiting for another connection.
 * 
 * fdServer: the file descriptor of the server socket
 * words: the list of words to use for cracking
 */
void process_connections(int fdServer, WordList words) {
    int fd;
    struct sockaddr_in fromAddr;
    socklen_t fromAddrSize;

    // Repeatedly accept connections and process data (capitalise)
    for (; 1;) {
        fromAddrSize = sizeof(struct sockaddr_in);
	// Block, waiting for a new connection. (fromAddr will be populated
	// with address of client)
        fd = accept(fdServer, (struct sockaddr*) &fromAddr, &fromAddrSize);
        if (fd < 0) {
            perror("Error accepting connection");
            exit(1);
        }
     
	// Turn our client address into a hostname and print out both 
        // the address and hostname as well as the port number
        char hostname[NI_MAXHOST];
        int error = getnameinfo((struct sockaddr*)&fromAddr, 
                fromAddrSize, hostname, NI_MAXHOST, NULL, 0, 0);
        if (error) {
            exit_code_four();
        }

        Client* client = malloc(sizeof(Client));
        //client->fd = fd;
        client->clientFile = fdopen(fd, "r+");
        client->fd = fd;
        client->words = words;

	pthread_t threadId;
	pthread_create(&threadId, NULL, client_thread, client);
	pthread_detach(threadId); // need to join later
    }
}

int main(int argc, char** argv) {
    CmdArg para = pre_run_checking(argc, argv);
    
    //open dictionary to read
    FILE* dictionary = fopen(para.dictionary, "r");
    if (dictionary == NULL) {
    	exit_code_two(para.dictionary);
    }
    //array contains valid words
    WordList words = read_dictionary(dictionary);
    fclose(dictionary);

    int fdServer = open_listen(para.port, para.maxconnections);
    process_connections(fdServer, words);
    free(words.wordArray);
    return 0;
}
