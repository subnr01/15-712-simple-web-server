#include <sys/socket.h>       // socket definitions
#include <sys/types.h>        // socket types
#include <arpa/inet.h>        // inet (3) funtions
#include <unistd.h>           // misc. UNIX functions
#include <signal.h>           // signal handling
#include <stdlib.h>           // standard library
#include <stdio.h>            // input/output library
#include <string.h>           // string library
#include <errno.h>            // error number library
#include <fcntl.h>            // for O_* constants
#include <sys/mman.h>         // mmap library
#include <sys/types.h>        // various type definitions
#include <sys/stat.h>         // more constants
#include <pthread.h>

// global constants
#define LISTENQ 10            // number of connections
#define MAX_CLIENTS 30

static int list_s;                   // listening socket
static short int port;       //  port number
static int connClose;

static int reuseaddr = 1; /* True */
static int client_sockets[MAX_CLIENTS];

// structure to hold the return code and the filepath to serve to client.
typedef struct {
	int returncode;
	char *filename;
} httpRequest;

// headers to send to clients
static char *header200Fmt = "HTTP/1.1 200 OK\r\nServer: 15-712 Proj v0.1\r\nContent-Type: text/html%s\r\n\r\n";
static char *header400Fmt = "HTTP/1.1 400 Bad Request\r\nServer: 15-712 Proj v0.1\r\nContent-Type: text/html%s\r\n\r\n";
static char *header404Fmt = "HTTP/1.1 404 Not Found\r\nServer: 15-712 Proj v0.1\r\nContent-Type: text/html%s\r\n\r\n";

// get a message from the socket until a blank line is recieved
char *getMessage(int fd) {
    // A file stream
    FILE *sstream;
    
    // Try to open the socket to the file stream and handle any failures
    if( (sstream = fdopen(fd, "r")) == NULL)
    {
        fprintf(stderr, "Error opening file descriptor in getMessage()\n");
        exit(EXIT_FAILURE);
    }

    // Size variable for passing to getline
    size_t size = 1;
    
    char *block;
    
    // Allocate some memory for block and check it went ok
    if( (block = malloc(sizeof(char) * size)) == NULL )
    {
        fprintf(stderr, "Error allocating memory to block in getMessage\n");
        exit(EXIT_FAILURE);
    }
  
    // Set block to null    
    *block = '\0';
    
    // Allocate some memory for tmp and check it went ok
    char *tmp;
    if( (tmp = malloc(sizeof(char) * size)) == NULL )
    {
        fprintf(stderr, "Error allocating memory to tmp in getMessage\n");
        exit(EXIT_FAILURE);
    }
    // Set tmp to null
    *tmp = '\0';
    
    // Int to keep track of what getline returns
    int end;
    // Int to help use resize block
    int oldsize = 1;
    // check if the getline is ran the first time
    int first = 1;

    // While getline is still getting data
    while( (end = getline( &tmp, &size, sstream)) >= 0)
    {
        if (end == 0 && first){
            struct sockaddr_in address;
            int addrlen = sizeof(address);
            getpeername(fd , (struct sockaddr*)&address , (socklen_t*)&addrlen);
            printf("Host disconnected , ip %s , port %d \n" , inet_ntoa(address.sin_addr) , ntohs(address.sin_port));             
            close(fd);
            free(block);
            free(tmp);
            return NULL;
        } 

        // If the line its read is a caridge return and a new line were at the end of the header so break
        if( strcmp(tmp, "\r\n") == 0)
        {
            break;
        }
        
        // Resize block
        block = realloc(block, size+oldsize);
        // Set the value of oldsize to the current size of block
        oldsize += size;
        // Append the latest line we got to block
        strcat(block, tmp);
    }
    
    // Free tmp a we no longer need it
    free(tmp);
    
    // Return the header
    return block;

}

// send a message to a socket file descripter
int sendMessage(int fd, char *msg) {
    return write(fd, msg, strlen(msg));
}

// Extracts the filename needed from a GET request and adds public_html to the front of it
char * getFileName(char* msg)
{
    // Variable to store the filename in
    char * file;
    // Allocate some memory for the filename and check it went OK
    if( (file = malloc(sizeof(char) * strlen(msg))) == NULL)
    {
        fprintf(stderr, "Error allocating memory to file in getFileName()\n");
        exit(EXIT_FAILURE);
    }
    
    // Get the filename from the header
    sscanf(msg, "GET %s HTTP/1.1", file);
    
    // Allocate some memory not in read only space to store "public_html"
    char *base;
    if( (base = malloc(sizeof(char) * (strlen(file) + 18))) == NULL)
    {
        fprintf(stderr, "Error allocating memory to base in getFileName()\n");
        exit(EXIT_FAILURE);
    }
    
    char* ph = "public_html";
    
    // Copy public_html to the non read only memory
    strcpy(base, ph);
    
    // Append the filename after public_html
    strcat(base, file);
    
    // Free file as we now have the file name in base
    free(file);
    
    // Return public_html/filetheywant.html
    return base;
}

// parse a HTTP request and return an object with return code and filename
httpRequest parseRequest(char *msg){
    httpRequest ret;
       
    // A variable to store the name of the file they want
    char* filename;
    // Allocate some memory to filename and check it goes OK
    if( (filename = malloc(sizeof(char) * strlen(msg))) == NULL)
    {
        fprintf(stderr, "Error allocating memory to filename in parseRequest()\n");
        exit(EXIT_FAILURE);
    }
    // Find out what page they want
    filename = getFileName(msg);
    
    // Check if its a directory traversal attack
    char *badstring = "..";
    char *test = strstr(filename, badstring);
    
    // Check if they asked for / and give them index.html
    int test2 = strcmp(filename, "public_html/");
    
    // Check if the page they want exists 
    FILE *exists = fopen(filename, "r" );
    
    // If the badstring is found in the filename
    if( test != NULL )
    {
        // Return a 400 header and 400.html
        ret.returncode = 400;
        ret.filename = "400.html";
    }
    
    // If they asked for / return index.html
    else if(test2 == 0)
    {
        ret.returncode = 200;
        ret.filename = "public_html/index.html";
    }
    
    // If they asked for a specific page and it exists because we opened it sucessfully return it 
    else if( exists != NULL )
    {
        
        ret.returncode = 200;
        ret.filename = filename;
        // Close the file stream
        fclose(exists);
    }
    
    // If we get here the file they want doesn't exist so return a 404
    else
    {
        ret.returncode = 404;
        ret.filename = "404.html";
    }
    
    // Return the structure containing the details
    return ret;
}

// print a file out to a socket file descriptor
int sendFile(int fd, char *filename) {
  
    /* Open the file filename and echo the contents from it to the file descriptor fd */
    
    // Attempt to open the file 
    FILE *read;
    if( (read = fopen(filename, "r")) == NULL)
    {
        fprintf(stderr, "Error opening file in sendFile()\n");
        exit(EXIT_FAILURE);
    }
    
    // Get the size of this file for printing out later on
    int totalsize;
    struct stat st;
    stat(filename, &st);
    totalsize = st.st_size;
    
    // Variable for getline to write the size of the line its currently printing to
    size_t size = 1;
    
    // Get some space to store each line of the file in temporarily 
    char *temp;
    if(  (temp = malloc(sizeof(char) * size)) == NULL )
    {
        fprintf(stderr, "Error allocating memory to temp in sendFile()\n");
        exit(EXIT_FAILURE);
    }
    
    // Int to keep track of what getline returns
    int end;
    
    // While getline is still getting data
    while( (end = getline( &temp, &size, read)) > 0)
    {
        if (sendMessage(fd, temp) < 0){
            printf("Problem with sending message\n");
        }
    }
    
    // Final new line
    if (sendMessage(fd, "\n") < 0){
        printf("Problem with sending message\n");
    } 
    
    // Free temp as we no longer need it
    free(temp);
    
    // Return how big the file we sent out was
    return totalsize;
  
}

// clean up listening socket on ctrl-c
void cleanup(int sig) {
    
    printf("Cleaning up connections and exiting.\n");
    
    // try to close the listening socket
    if (close(list_s) < 0) {
        fprintf(stderr, "Error calling close()\n");
        exit(EXIT_FAILURE);
    }
        
    // exit with success
    exit(EXIT_SUCCESS);
}

int sendHeader(int fd, int returncode)
{
    char header[2048]; 

    switch (returncode)
    {
        case 200:
        if (connClose){
            sprintf(header, header200Fmt, "\r\nConnection: close");
        } else {
            sprintf(header, header200Fmt, "");
        }
        sendMessage(fd, header);
        return strlen(header);
        break;
        
        case 400:
        if (connClose){
            sprintf(header, header400Fmt, "\r\nConnection: close");
        } else {
            sprintf(header, header400Fmt, "");
        }
        sendMessage(fd, header);
        return strlen(header);
        break;
        
        case 404:
        if (connClose){
            sprintf(header, header404Fmt, "\r\nConnection: close");
        } else {
            sprintf(header, header404Fmt, "");
        }
        sendMessage(fd, header);
        return strlen(header);
        break;
    }

    return -1;
}

void handle(int sock, char* buf, fd_set *set, int i){
    printf("sending HTTP header to back to socket %d\n", sock);

    httpRequest details = parseRequest(buf);
    printf("parsed the HTTP request %d\n", sock);

    sendHeader(sock, details.returncode);
    printf("sent the HTTP header %d\n", sock);

    sendFile(sock, details.filename);
    printf("sent the file content as well %d\n", sock);

    close(sock);
    FD_CLR(sock, set);
    client_sockets[i] = 0;
}

int main(int argc, char *argv[]) {
    fd_set readfds;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int max_sd, new_socket, i, valread;
    struct sockaddr_in servaddr; //  socket address structure

    char buffer[4096];

    // set up signal handler for ctrl-c
    (void) signal(SIGINT, cleanup);
    
    if (argc != 3){
        printf("Must specify a port number and whether to use connection close\n");
        return -1;
    }
    port = atoi(argv[1]);
    connClose = atoi(argv[2]);

    // create the listening socket
    if ((list_s = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
        fprintf(stderr, "Error creating listening socket.\n");
        exit(EXIT_FAILURE);
    }

    /* Enable the socket to reuse the address */
    if (setsockopt(list_s, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int)) == -1) {
        printf("Let us reuse the address on the socket\n");
        exit(EXIT_FAILURE);
    }

    // set all bytes in socket address structure to zero, and fill in the relevant data members
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port        = htons(port);
    
    // bind to the socket address
    if (bind(list_s, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0 ) {
        fprintf(stderr, "Error calling bind()\n");
        exit(EXIT_FAILURE);
    }
    
    // Listen on socket list_s
    if( (listen(list_s, 10)) == -1) {
        fprintf(stderr, "Error Listening\n");
        exit(EXIT_FAILURE);
    } 

    printf("Listen on the socket\n");

    socklen_t addr_size = sizeof(servaddr);
    for (i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = 0;
    }

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(list_s, &readfds);
        max_sd = list_s;

        for (i = 0 ; i < MAX_CLIENTS ; i++) {
            int sd = client_sockets[i];
            
            //if valid socket descriptor then add to read list
            if(sd > 0){
                FD_SET(sd , &readfds);
            }
            
            //highest file descriptor number, need it for the select function
            if(sd > max_sd){
                max_sd = sd;
            }
        }

        if (select(max_sd + 1, &readfds, NULL, NULL, NULL) == -1) {
            printf("Something is wrong with the select wait call\n");
            return -1;
        }

         //If something happened on the master socket , then its an incoming connection
        if (FD_ISSET(list_s, &readfds)) 
        {
            printf("Select has new connection\n");
            if ((new_socket = accept(list_s, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0){
                perror("accept");
                exit(EXIT_FAILURE);
            }
         
            //inform user of socket number - used in send and receive commands
            printf("New connection , socket fd is %d , ip is : %s , port : %d \n" , new_socket , inet_ntoa(address.sin_addr) , ntohs(address.sin_port));
                          
            //add new socket to array of sockets
            for (i = 0; i < MAX_CLIENTS; i++) 
            {
                //if position is empty
                if( client_sockets[i] == 0 )
                {
                    client_sockets[i] = new_socket;
                    printf("Adding to list of sockets as %d\n" , i);
                    break;
                }
            }
        }
         
        int hasMAx = 0;
        for (i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            
            if (FD_ISSET(sd , &readfds)) {
                printf("Select received a http request with sd %d\n", sd);
                hasMAx = 1;
                //Check if it was for closing , and also read the incoming message
                memset( buffer, '\0', sizeof(char)*4096 );
                if ((valread = read(sd , buffer, 4096)) == 0)
                {
                    getpeername(sd , (struct sockaddr*)&address , (socklen_t*)&addrlen);
                    printf("Host disconnected , ip %s , port %d \n" , inet_ntoa(address.sin_addr) , ntohs(address.sin_port));
                     
                    close(sd);
                    client_sockets[i] = 0;
                }
                else
                {
                    buffer[valread] = '\0';
                    printf("Received buffer %s\n", buffer);
                    handle(sd, buffer, &readfds, i);
                }
            }
        }
        if (!hasMAx){
            printf("No max client select me...\n");
        }
    }

    close(list_s);
    return EXIT_SUCCESS;
}
