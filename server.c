#include "client_server.h"                                                           
#include <pthread.h>

//
// Concurrent hash table management server Algorithm:
// 
//    whiile(1) {
//        poll socket for CMD;
//        if (CMD != NULL) {
//            if (CMD == STOR) {
//                create writer_worker thread;
//                acquire_writer_lock;
//                add_element_to_hash;
//                release_writer_lock;
//                return SUCCESS to client via main thread;
//            }
//            else if (CMD == RETR) {
//                create reader_worker thread;
//                while (lock is ON) {
//                    wait();
//                }
//                get_value_for_key;
//                if(MATCH found) {
//                    return value to client via main thread;
//                } else {
//                    return NO SUCCESS to client via main thread;
//                }
//            }
//            close worker thread;
//        } // end CMD 
//    } // end while(1)
//
//
// Client-Server communication Protocol:
//
// 1. If lookup is SUCCESS, CMD_RETR returns value of first MATCHED key to client.
// 2. If lookup is NO SUCCESS, CMD_RETR returns NO SUCCESS to client.
// 3. If key doesn't exist in hash yet, CMD_STOR adds to hash and returns index.
// 4. If key already exists in hash, CMD_STOR returns the bucket index of key.
//
// Compilation and test:
// 	$ gcc ./server.c -o server -pthread && ./server 7861
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


/* Hash table size and accomodating N concurrent client and worker threads */
#define HASH_TABLE_SIZE        10009 /* choosing lowest 5 digit prime number */
#define NUM_STOR_CLIENTS       3
#define NUM_RETR_CLIENTS       7
#define NUM_CLIENT_THREADS     (NUM_STOR_CLIENTS + NUM_RETR_CLIENTS)
#define NUM_WORKER_THREADS     101
#define VALID_KEY_LIMIT        (HASH_TABLE_SIZE*HASH_TABLE_SIZE)
#define NUM_CLIENT_OPERATIONS  199
#define INVALID_BUCKET_INDEX   0xFFFFFFFF

/* Note: Enable only one of the modes. In UT mode, running client is not required */
//#define UNIT_TEST_MODE
#define PRODUCTION_CODE_MODE

unsigned int debug_print_flag = 0;
#define PRINT(x) {if(debug_print_flag==1) printf(x);}

/* with simultaneous threads sharing these global resources */
static int cbi = 0, ccbi = 0;
int hash_table_size = HASH_TABLE_SIZE;
pthread_t p[NUM_WORKER_THREADS]={0}; /* iterator is cbi  */
pthread_t q[NUM_CLIENT_THREADS]={0}; /* iterator is ccbi */
int idx=0;
pthread_rwlock_t rw_lock = PTHREAD_RWLOCK_INITIALIZER;
unsigned int sleep(unsigned int seconds);

/* hash table collision list (htcl) */
typedef struct list_t {
	unsigned int   key;
	unsigned int   value;
	unsigned int   bucket_idx;
	struct list_t *next;
} htcl;

/* hash table of buckets with each bucket has chained collision list of htcl nodes */
typedef struct _hash_table_t {
	htcl * hash_bucket[HASH_TABLE_SIZE];
} hash_table_t;

/* global hash table hence the need of locks */
hash_table_t *my_hash_table= NULL;

/* two way struct for passing value back and forth between main and worker threads */
typedef struct thread_data_t {
	unsigned int        key;
	unsigned int        value;
	bool                status;
	unsigned int        bucket_idx;
	hash_table_t       *hash_table;
	pthread_rwlock_t   *rw_lock;
} thread_data;

/* setting up hash table */
static inline hash_table_t *create_hash_table(void) {

	/* allocate memory for hash table structure */ 
	hash_table_t *table_ptr = (hash_table_t *) malloc(sizeof(hash_table_t));
	if (table_ptr == NULL)	return NULL;

	/* allocate and init hash table itself */ 
	for (idx=0; idx < HASH_TABLE_SIZE; idx++) {
		table_ptr->hash_bucket[idx] = (htcl *) malloc(sizeof(htcl));
		if (table_ptr->hash_bucket[idx] == NULL)	return NULL;
		table_ptr->hash_bucket[idx]->next = NULL;	
		table_ptr->hash_bucket[idx]->value = 0;	
		table_ptr->hash_bucket[idx]->bucket_idx = 0;	
	}

	return table_ptr;
}

/* computing hash value for a given search key */
static inline unsigned int hash(hash_table_t *hashtable, unsigned int key) {

	/* choosing some random O(1) hash function here */
	unsigned int hashval = 0xDEADBEEF;
	hashval ^= key ^ (key >> 8) ^ (key >> 16) ^ (key >> 24);
	hashval ^= hashval ^ (hashval >> 8) ^ (hashval >> 16) ^ (hashval >> 24);
	return (hashval % hash_table_size);
}

/* used by CMD_RETR and CMD_STOR */
static inline htcl * lookup (hash_table_t *hashtable, bool ignore_value,
                   unsigned int key, unsigned int value){

	htcl * node = NULL;
	unsigned int hashval = 0;

	if (!hashtable) return NULL;

	hashval = hash(hashtable, key);
	for (node = hashtable->hash_bucket[hashval]; node != NULL; node=node->next){
	    if((ignore_value && key == node->key) ||
	       (!ignore_value && key == node->key && value == node->value)) {

		node->bucket_idx = hashval; 
		printf("\nLOOKUP SUCCESS (key,val) --> (0x%x, 0x%x) !!!!",\
			key, node->value);
		return node;
	    }
	}
	PRINT("\n LOOKUP FAILED ...");
	return NULL;
}

/* only used by CMD_STOR */
static inline void add_entry_to_bucket (hash_table_t *hash_table,\
                                        thread_data *tdata) {

	htcl *iterator = NULL;
	unsigned int hashval = hash(hash_table, tdata->key);

	/* first create memory and init value for the node to be added */
	htcl * node = (htcl *) malloc (sizeof(htcl));

	if (!node) {PRINT("\nFATAL ERROR"); exit(1);}

	node->key         = tdata->key;
	node->value       = tdata->value;
	node->bucket_idx  = hashval;
	tdata->bucket_idx = hashval;
	node->next        = NULL;

	/* now seek the collision list end for the bucket index and insert */
	iterator = hash_table->hash_bucket[hashval];
	while(iterator && iterator->next) {
		iterator = iterator->next;
	}
	iterator->next = node;
}

/* avoid wasting heap memory of the system */
static inline void free_hash_table (hash_table_t * table_ptr) {
	
	htcl * collision_list_node, *temp;

	/* first free the hash table elements with collision list nodes */
	for (idx=0; idx < HASH_TABLE_SIZE; idx++) {
		collision_list_node = table_ptr->hash_bucket[idx];
		while (collision_list_node != NULL) {
			temp = collision_list_node;
			collision_list_node = collision_list_node->next;
			free(temp);
		}	
	}

	/* now free the hash table itself */
	free (table_ptr);
}

// reader callback
//
// 1. If key does exist in hash, CMD_RETR returns the bucket index of key to client
// 2. If key doesn't exist in hash yet, CMD_RETR returns NOSUCCESS result to client
//
void * rcb (void * arg) {

	thread_data *tdata=(thread_data *)arg;

	/* acquire read lock for the shared global hash table */
	pthread_rwlock_t *p = tdata->rw_lock;
	if (pthread_rwlock_rdlock(p) != 0) {
		perror("reader_thread: pthread_rwlock_rdlock error");
		exit(__LINE__);
	}

	/* search the hash table */
	htcl *lookedupnode = lookup(tdata->hash_table,true,tdata->key,tdata->value);

	/* release read lock for the shared global hash table */
	if (pthread_rwlock_unlock(p) != 0) {
		perror("reader thread: pthred_rwlock_unlock error");
		exit(__LINE__);
	}

	if (lookedupnode != NULL) {
		/* the lookup yielded MATCH */
		tdata->status     = CMD_SUCCESS;
		tdata->bucket_idx = lookedupnode->bucket_idx;
	} else {
		/* the lookup yielded NO MATCH */
		tdata->status     = CMD_NOSUCCESS;
		tdata->bucket_idx = INVALID_BUCKET_INDEX;
	}

	PRINT("\n>exiting reader thread");
	pthread_exit(0);
}

// writer callback
//
// 3. If key doesn't exist in hash yet, CMD_STOR adds to hash and returns index
// 4. If key already exists in hash, CMD_STOR returns the bucket index of key
//
void * wcb (void * arg) {

	thread_data *tdata=(thread_data *)arg;

	htcl *lookedupnode = lookup(tdata->hash_table,false,tdata->key,tdata->value);

	/* for CMD_STOR we always declare CMD_SUCCESS to client */
	tdata->status     = CMD_SUCCESS;

	if (lookedupnode == NULL) {
		/* acquire write lock for the shared global hash table */
		pthread_rwlock_t *p = tdata->rw_lock;
		if (pthread_rwlock_wrlock(p) != 0) {
			perror("writer_thread: pthread_rwlock_wrlock error");
			exit(__LINE__);
		}

		/* the lookup yielded NO MATCH */
		add_entry_to_bucket(my_hash_table, tdata);

		/* release write lock for the shared global hash table */
		if (pthread_rwlock_unlock(p) != 0) {
			perror("writer thread: pthred_rwlock_unlock error");
			exit(__LINE__);
		}
	} else {
		/* the lookup yielded MATCH */
		tdata->bucket_idx = lookedupnode->bucket_idx;
	}

	PRINT("\n>exiting writer thread");
	pthread_exit(0);
}

/* main entry point for all client commans after polling TCP socket */
static inline bool handle_cmd(const bool cmd, unsigned int key,\
                              unsigned int *value) {

	thread_data tdata;
	tdata.key        = key;
	tdata.value      = *value;
	tdata.status     = CMD_NOSUCCESS;
	tdata.bucket_idx = INVALID_BUCKET_INDEX;
	tdata.hash_table = my_hash_table;
	tdata.rw_lock    = &rw_lock;

	printf("\nhandling %s (key, value) -> (0x%x, 0x%x)...", \
		(CMD_STOR == cmd) ? "STOR" : "RETR", key, *value);

	if (CMD_STOR == cmd) {
	    PRINT("\nCreate writer thread...");
	    pthread_create(&p[cbi], NULL, wcb, (void *)&tdata);
	} else if (CMD_RETR == cmd) {
	    PRINT("\nCreate reader thread...");
	    pthread_create(&p[cbi], NULL, rcb, (void *)&tdata);
	}
	
	/* invoke the thread and ensure circular buffer indexing (cbi) for threads */
	pthread_join(p[cbi], NULL);
	cbi++; cbi%=NUM_WORKER_THREADS;

	printf("\nResult %s",\
		(tdata.status == CMD_SUCCESS)?"CMD SUCCESS! ":"CMD NO SUCCESS!\n");
	if(tdata.status == CMD_SUCCESS) {
	    printf("Key 0x%x, Value 0x%x, Bucket 0x%x\n",\
	    tdata.key, tdata.value, tdata.bucket_idx);
	}
	PRINT("------------------------------");

	return tdata.status;        
}

#ifdef UNIT_TEST_MODE 
/* test stub for STOR command */
static inline void test_STOR (unsigned int key, unsigned int value) {
	handle_cmd(CMD_STOR, key, &value);
}

/* test stub for RETR command */
static inline void test_RETR (unsigned int key) {
	unsigned int value = 0xdeadbeef;
	handle_cmd(CMD_RETR, key, &value);
}

/* create clients who are doing RETR operations for random key */
void * rclient (void * arg) {

	thread_data *tdata=(thread_data *)arg;
	test_RETR(tdata->key);
        /* let thread to sleep for random seconds, to simulate parallel threads */
	sleep(rand()%NUM_RETR_CLIENTS);
	pthread_exit(0);
}

/* create clients who are doing STOR operations for random key */
void * wclient (void * arg) {

	thread_data *tdata=(thread_data *)arg;
	test_STOR(tdata->key, tdata->value);
        /* let thread to sleep for random seconds, to simulate parallel threads */
	sleep(rand()%NUM_STOR_CLIENTS);
	pthread_exit(0);
}

/* test stubs for clients who issue RETR */
void simulate_reader_clients(thread_data tdata){

	unsigned int rclients = 0;

	/* create NUM_RETR_CLIENTS reader threads with random key */	
	for(; rclients < NUM_RETR_CLIENTS; rclients++) {
		tdata.key = rand() % VALID_KEY_LIMIT;
		printf("\nrandom search key: %u ", tdata.key);
	  	pthread_create(&q[ccbi], NULL, rclient, (void *)&tdata);
	  	/* ensure circular buffer indexing (ccbi) for reader threads */
	 	pthread_join(q[ccbi], NULL);
	  	ccbi++; ccbi%=NUM_CLIENT_THREADS;
	}
}

/* test stubs for clients who issue STOR */
void simulate_writer_clients(thread_data tdata){

	unsigned int wclients = 0;

	/* create NUM_STOR_CLIENTS writer threads with random key */	
	for(; wclients < NUM_STOR_CLIENTS; wclients++) {
		/* creating STOR command with random key, value pairs */ 
		tdata.key   = rand() % VALID_KEY_LIMIT;
		tdata.value = (rand()*rand())% VALID_KEY_LIMIT;
		printf("\nrandom search key: %u (0x%x)", \
			tdata.key, tdata.key);
	  	pthread_create(&q[ccbi], NULL, wclient, (void *)&tdata);
	  	/* ensure circular buffer indexing (ccbi) for writer threads */
	 	pthread_join(q[ccbi], NULL);
	  	ccbi++; ccbi%=NUM_CLIENT_THREADS;
	}
}

/* test stub for requirement 1 */
static inline void test_parallel_store_retrieve_operations() {

	unsigned int num = 0;

	/* generate random key and initialize client thread data */
	thread_data tdata;
	tdata.status     = CMD_NOSUCCESS;
	tdata.bucket_idx = INVALID_BUCKET_INDEX;
	tdata.hash_table = my_hash_table;
	/* 
         * Note: clients can not and need not acquire lock to hash table,
	 * since the command handler functions need to take care of it.
	 */
	tdata.rw_lock    = NULL;
	
	/* init seed for the random key to be searched/stored in hash */
	srand(time(NULL));
	
        /* simulating random number of clients RETR / STOR operations */
	for(; num < NUM_CLIENT_OPERATIONS; num++) {
		printf("\n######## Client operation number (%d) ########", num);
		simulate_reader_clients(tdata);
		simulate_writer_clients(tdata); simulate_reader_clients(tdata);
		simulate_writer_clients(tdata); simulate_reader_clients(tdata);
	}

	return;
}

/* test stub for requirement 2 */
static inline void test_sequential_store_retrieve_operations() {
	
	/* issue RETR with empty hashtable (expected result: NO SUCCESS) */
	test_RETR(0x9001);

	/* issue STOR with empty hashtable (expected result: SUCCESS) */
	test_STOR(0x1234, 0xabcd4321);
	test_STOR(0x5678, 0xbcdebcda);
	test_STOR(0x9001, 0xcdef1234);
	test_STOR(0x101,0xdefa7777);

	/* issue RETR with hashtable containing key (expected result: SUCCESS) */
	/* note: the result should display the value */
	test_RETR(0x101);
	test_RETR(0x9001);
	test_RETR(0x5678);
	test_RETR(0x1234);

	/* issue STOR with hashtable already having key (expected result: SUCCESS) */
	/* note: the result should display bucket index same as obtained above */
	test_STOR(0x5678, 0xbcdebcda);
	test_STOR(0x1234, 0xabcd4321);

	/* issue STOR with hashtable already having key (expected result: SUCCESS) */
        /* note this time the value in key-value pair is different */
	test_STOR(0x1234, 0xcaca2345);
	test_STOR(0x5678, 0xdede4567);
        
	/* issue RETR with hashtable containing key (expected result: SUCCESS) */
	/* note	this time the value returned should be the first one to match */
	test_RETR(0x5678);
	test_RETR(0x1234);

	return;
}
#endif

/* basic CLI validation */
static inline void validate_input(int argc, char **argv) {
#ifdef PRODUCTION_CODE_MODE
     if (argc < 2) {
         fprintf(stderr,"usage:  %s port\nExample:  %s 7891\n", argv[0], argv[0]);
         exit(1);
     }
#endif
#ifdef UNIT_TEST_MODE
     if (argc < 1) {
         fprintf(stderr,"usage:  %s\nExample:  %s\n", argv[0], argv[0]);
         exit(1);
     }
#endif
}

#ifdef PRODUCTION_CODE_MODE
/* setup TCP socket to client */
static inline void setup_server_side_socket_parameters(int sockfd,\
                                         int *newsockfd, int portno,\
                                         char **argv, socklen_t clilen,\
                                         struct sockaddr_in serv_addr,\
                                         struct sockaddr_in cli_addr) {

     /* basic socket server side setup */
     sockfd = socket(AF_INET, SOCK_STREAM, 0);
     if (sockfd < 0) 
        error("ERROR opening socket");
     bzero((char *) &serv_addr, sizeof(serv_addr));
     portno = atoi(argv[1]);
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(portno);
     if (bind(sockfd, (struct sockaddr *) &serv_addr,
              sizeof(serv_addr)) < 0) 
              error("ERROR on binding");
     listen(sockfd,5);
     clilen = sizeof(cli_addr);
     *newsockfd = accept(sockfd, 
                 (struct sockaddr *) &cli_addr, 
                 &clilen);
     if (*newsockfd < 0) 
          error("ERROR on accept");

     /* init seed for the random key to be searched/stored in hash */
     srand(time(NULL));                                                           
}

/* construct message to be sent to client after handling command */
static inline void construct_response (char *buffer, buffer_data *bdata) {

    /* 0: NO SUCCESS, 1: SUCCESS */
    if(!bdata->status) {
        bdata->value = 0xdeadbeef;
    }
    printf("\nresponse from server to client: ");
    encode_key_value_to_message_buffer(buffer, bdata);
}

/* to poll the TCP socket continously and handle client commands (if any) */
static inline void poll_server_side_socket_to_process_command(int newsockfd) {

     unsigned int seq_num=0, n;
     char buffer[SOCKET_BUFFER_LEN];
     buffer_data bdata;
     bool status = CMD_NOSUCCESS;

     while (1) {
         /* server receiving command from client */
         CLEAR_SOCKET_BUFFER;
         bdata.seq_num = seq_num++;
         n = read(newsockfd,buffer,(SOCKET_BUFFER_LEN-1));
         if (n < 0) error("ERROR reading from socket");
         printf("\nRequest from client: ");
         decode_key_value_from_message_buffer(buffer, &bdata);
         
         /* key step to process the command by the concurrent hash infra */
         status = handle_cmd(bdata.command, bdata.key, &(bdata.value));
         bdata.status = status;
         CLEAR_SOCKET_BUFFER;
         construct_response(buffer, &bdata);

         /* server sending response to the client */
         n = write(newsockfd,buffer,strlen(buffer));
         if (n < 0) error("ERROR writing to socket");
         printf("\n----------------------");
     }
}
#endif

/* main driver function for server */
int main(int argc, char *argv[]) {
	int sockfd, newsockfd, portno;
	socklen_t clilen;
	struct sockaddr_in serv_addr, cli_addr;

	my_hash_table = create_hash_table();
	if(my_hash_table==NULL) {
		printf("\nFailed to create hash table");
		exit(__LINE__);
	}

	/* CLI validation */
	validate_input(argc, argv);

#ifdef UNIT_TEST_MODE 
	/* requirement 1 */
	test_sequential_store_retrieve_operations();

	/* requirement 2 */
	test_parallel_store_retrieve_operations();
#endif

#ifdef PRODUCTION_CODE_MODE
	setup_server_side_socket_parameters(sockfd, &newsockfd, portno, argv,\
                                            clilen, serv_addr, cli_addr);
	poll_server_side_socket_to_process_command(newsockfd);
        close(newsockfd);
        close(sockfd); 
#endif
	/* switch off lights while exiting conf room */
	free_hash_table(my_hash_table);

	return 0;
}
