#include "client_server.h"

// Client-Server communication Protocol:
//
// 1. If lookup is SUCCESS, CMD_RETR returns value of first MATCHED key to client.
// 2. If lookup is NO SUCCESS, CMD_RETR returns NO SUCCESS to client.
// 3. If key doesn't exist in hash yet, CMD_STOR adds to hash and returns index.
// 4. If key already exists in hash, CMD_STOR returns the bucket index of key.
//
// Compilation and test:
//      $ gcc client.c -o client && ./client localhost 7861
//
//                                                                                
// Client to Server message format
//
//      31              15             0
//    +---+------------+----------------+
//    | C |  reserved  |       Key      |
//    +---+------------+----------------+
//    |          value or bucket index  |
//    +---+------------+----------------+
//   
//    C - 0 (CMD_STOR), 1 (CMD_RETR) 
//
// Server to Client message format
//
//      31              15             0
//    +---+------------+----------------+
//    | S |  reserved  |       Key      |
//    +---+------------+----------------+
//    |          value or bucket index  |
//    +---+------------+----------------+
//
//    S - 0 (NO SUCCESS), 1 (SUCCESS)
//

/* basic CLI validation */
static inline void validate_input(int argc, char **argv) {

    if (argc < 3) {
       fprintf(stderr,"usage %s hostname port\n", argv[0]);
       exit(0);
    }
}

/* setup TCP socket to server */
static inline void setup_client_side_socket_parameters (int *sockfd, int portno,\
                   char **argv, struct sockaddr_in serv_addr) {

    struct hostent *server;

    /* basic socket client side setup */                                            
    portno = atoi(argv[2]);
    *sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (*sockfd < 0) 
        error("ERROR opening socket");
    server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);
    if (connect(*sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 
        error("ERROR connecting");

    /* init seed for the random key to be searched/stored in hash */
    srand(time(NULL));                                                    
}

/* construct message to be sent to server over TCP socket -- for RETR */
static inline void construct_RETR_command (char *buffer, buffer_data *bdata){

    bdata->command = true; /* 0: STOR, 1: RETR */ 
    GET_RANDOM_KEY(bdata->key);	
    bdata->value = 0xdeadbeef;
    printf("\nRETR cmd from client to server: ");
    encode_key_value_to_message_buffer(buffer, bdata);
}

/* construct message to be sent to server over TCP socket -- for STOR */
static void construct_STOR_command (char *buffer, buffer_data *bdata) {

    bdata->command = false; /* 0: STOR, 1: RETR */ 
    GET_RANDOM_KEY(bdata->key);	
    GET_RANDOM_VALUE(bdata->value);	
    printf("\nSTOR cmd from client to server: ");
    encode_key_value_to_message_buffer(buffer, bdata);
}

/* send command to server via TCP socket and parse server response */
static inline void simulate_clients_send_sequential_cmds_to_server(int sockfd) {

    unsigned int seq_num = 0, n;
    char buffer[SOCKET_BUFFER_LEN];
    buffer_data bdata;

    while(1) {	
        /* sending client commands over TCP socket after delay of 1 second */
        sleep (1);

        /* sending command from client to server */
        CLEAR_SOCKET_BUFFER;
        bdata.seq_num = seq_num++;
        (rand()%NUM_COMMANDS_SUPPORTED) ?\
            construct_STOR_command(buffer, &bdata):\
            construct_RETR_command(buffer, &bdata);
        n = write(sockfd,buffer,strlen(buffer));
        if (n < 0) error("ERROR writing to socket");

        /* receiving response from the server */
        CLEAR_SOCKET_BUFFER;
        n = read(sockfd,buffer,(SOCKET_BUFFER_LEN - 1));
        if (n < 0) error("ERROR reading from socket");
        printf("\nResponse from server: ");
	decode_key_value_from_message_buffer(buffer, &bdata);
        printf("\nResult seen by client %s",\
                 (bdata.status == CMD_SUCCESS) ? "SUCCESS" : "NO SUCCESS");
        printf("\n----------------------");
    }
}

/* main driver function for client */
int main(int argc, char *argv[])
{
    int sockfd, portno;
    struct sockaddr_in serv_addr;

    /* client operation */
    validate_input(argc, argv);
    setup_client_side_socket_parameters (&sockfd, portno, argv, serv_addr);
    simulate_clients_send_sequential_cmds_to_server(sockfd);

    close(sockfd);
    return 0;
}
