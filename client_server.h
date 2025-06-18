#include <stdio.h> 
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <errno.h>

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


/* all sizes and lengths in bytes */
#define NUM_COMMANDS_SUPPORTED     2
#define SOCKET_BUFFER_LEN          68
#define CLIENT_TO_SERVER_MSG_SIZE  8
#define SERVER_TO_CLIENT_MSG_SIZE  8
#define HASH_KEY_SIZE              16
#define HASH_VAL_SIZE              32
#define HASH_KEY_OFFSET            16
#define HASH_VAL_OFFSET            32
#define MESSAGE_BUFFER_SIZE        64
#define MASK_KEY                   0x0000FFFF
#define MASK_VALUE                 0xFFFFFFFF

/* all size in terms of nibbles */
#define KEY_SIZE_IN_MSG   4
#define VAL_SIZE_IN_MSG   8
#define KEY_OFFSET_IN_MSG 4
#define VAL_OFFSET_IN_MSG 8
#define MSG_ENCODING_BASE 16

/* Commands implemented in Server and possible results */
#define CMD_STOR             0
#define CMD_RETR             1
#define CMD_SUCCESS          1
#define CMD_NOSUCCESS        0

#define CLEAR_SOCKET_BUFFER   {bzero(buffer,SOCKET_BUFFER_LEN);}
#define GET_RANDOM_KEY(key)   {key = rand() & MASK_KEY;}
#define GET_RANDOM_VALUE(val) {val = (rand()^rand()) & MASK_VALUE;}

/* the data structure to hold values which are to be sent to socket buffer */
typedef struct buffer_data_t {
        unsigned int seq_num; 
        union {
                bool command; /* C bit indicating command: STOR / RETR */
                bool status;  /* S bit indicating result: SUCCESS / NO SUCCESS */
        };      
        unsigned short int key; /* considering 16 bit search key */
        unsigned int value; 
} buffer_data;

/* handle basic errors during socket operations */
void error(const char *msg)
{
    perror(msg);
    exit(1);
}

/* 
 * API to encode the lookup data in socket buffer before sending over TCP socket
 * 	input:  key, value, cmd/status
 *      output: data in message buffer for socket
 */
static inline void encode_key_value_to_message_buffer(char *buffer,\
                                                      buffer_data *bdata) {

    snprintf(buffer, MESSAGE_BUFFER_SIZE, "%d000%04x%08x",\
             bdata->command, bdata->key, bdata->value);
    printf("\n(%d) cmd %d, key 0x%x, value, 0x%x",\
             bdata->seq_num, bdata->command, bdata->key, bdata->value);
}

/* 
 * API to decode the lookup data from socket buffer after receiving over TCP socket
 *      input:  data in message buffer for socket
 * 	output: key, value, cmd/status
 */
static inline void decode_key_value_from_message_buffer(char *buffer,\
                                                        buffer_data *bdata) {

    int i;
    char server_key[KEY_SIZE_IN_MSG + 1];
    char server_val[VAL_SIZE_IN_MSG + 1]; 
    for (i = 0; i < KEY_SIZE_IN_MSG; i++) {
        server_key[i] = buffer [i + KEY_OFFSET_IN_MSG];
    }
    server_key[i] = '\0';
    for (i = 0; i < VAL_SIZE_IN_MSG; i++) {
        server_val[i] = buffer [i + VAL_OFFSET_IN_MSG];
    }
    server_val[i] = '\0';
                                                                                     
    bdata->command = (buffer[0] == '1') ? true : false;
    bdata->key = (unsigned int) strtol(server_key, NULL, MSG_ENCODING_BASE);
    bdata->value  = (unsigned int) strtol(server_val, NULL, MSG_ENCODING_BASE);
    printf("\n(%d) cmd %d key 0x%04x value 0x%08x",\
            bdata->seq_num, bdata->command, bdata->key, bdata->value);
}
