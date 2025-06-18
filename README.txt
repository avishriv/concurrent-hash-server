How is this project about:
	This is a quick project for the implementation of "concurrent hash table management at server side".
	
	
How to compile and run (test):
	$ gcc client.c -o client && ./client localhost 7861
	$ gcc server.c -o server -pthread && ./server 7861
	
	
What's the client-server communication format:
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

What's the client-server communication Protocol:
	// 1. If lookup is SUCCESS, CMD_RETR returns value of first MATCHED key to client.
	// 2. If lookup is NO SUCCESS, CMD_RETR returns NO SUCCESS to client.
	// 3. If key doesn't exist in hash yet, CMD_STOR adds to hash and returns index.
	// 4. If key already exists in hash, CMD_STOR returns the bucket index of key.

What's the hash table management algorithm:
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
