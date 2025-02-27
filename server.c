#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <unistd.h>
#include <pthread.h>
#include "vec.h"
#include <dirent.h>
#include <sys/stat.h>



///////////////////////////////////////////////////////////////////////////////////////////////////
// const
///////////////////////////////////////////////////////////////////////////////////////////////////
// user input /////////////////////////////////////////////////////////////////////////////////////
#define INITIAL_COMMAND "c"
#define EXIT_COMMAND "q"

// init vector ////////////////////////////////////////////////////////////////////////////////////
#define INIT_VECTOR_QUEUE_NAME "/init"
#define NEW_VECTOR_CREATED 1
#define VECTOR_ALREADY_EXISTS 0
#define VECTOR_CREATION_ERROR -1
#define INIT_VECTOR_QUEUE_MAX_MESSAGES 10
#define MAX_VECTOR_NAME_LEN 40
#define MAX_RESP_QUEUE_NAME_LEN 64

// message sent to this server to create a new vector
struct init_msg {
    char name[MAX_VECTOR_NAME_LEN];                 // new vector name
    int size;                                       // size of the vector
    char resp_queue_name[MAX_RESP_QUEUE_NAME_LEN];  // queue to which a response will be sent
};

#define INIT_MSG_SIZE sizeof(struct init_msg)

// set value in vector ////////////////////////////////////////////////////////////////////////////
#define SET_QUEUE_NAME "/set"
#define SET_QUEUE_MAX_MESSAGES 10
#define SET_SUCCESS 0
#define SET_FAIL -1

// message sent to this server to set a value in a particular vector at a specific position
struct set_msg {
    char name[MAX_VECTOR_NAME_LEN];                 // name of the vector to be modified
    int pos;                                        // index of the element to be modified
    int value;                                      // value to be put on the specified position
    char resp_queue_name[MAX_RESP_QUEUE_NAME_LEN];  // queue to which a response will be sent
};

#define SET_MSG_SIZE sizeof(struct set_msg)

// get value from vector //////////////////////////////////////////////////////////////////////////
#define GET_QUEUE_NAME "/get"
#define GET_QUEUE_MAX_MESSAGES 10
#define GET_SUCCESS 0
#define GET_FAIL -1

// message sent to this server to get a value from a particualr vector from a specific position
struct get_msg {
    char name[MAX_VECTOR_NAME_LEN];                 // name of the vector
    int pos;                                        // index of the requested element
    char resp_queue_name[MAX_RESP_QUEUE_NAME_LEN];  // queue to which a response will be sent
};

#define GET_MSG_SIZE sizeof(struct get_msg)

// message sent by this server to a client sending get_msg 
struct get_resp_msg {
    int value;  // if no error then contains value from requested position
    int error;  // 0 --> success; -1 --> fail
};

#define GET_RESP_MSG_SIZE sizeof(struct get_resp_msg)

// destroy vector /////////////////////////////////////////////////////////////////////////////////
#define DESTROY_QUEUE_NAME "/destroy"
#define DESTROY_QUEUE_MAX_MESSAGES 10
#define DESTROY_SUCCESS 1
#define DESTROY_FAIL -1

// message sent to this server to destroy a particular vector
struct destroy_msg {
    char name[MAX_VECTOR_NAME_LEN];                 // name of the vector to be destroyed
    char resp_queue_name[MAX_RESP_QUEUE_NAME_LEN];  // queue to which a response will be sent
};

#define DESTROY_MSG_SIZE sizeof(struct destroy_msg)

// general errors errors //////////////////////////////////////////////////////////////////////////
#define QUEUE_OPEN_ERROR 13
#define QUEUE_INIT_SUCCESS 1
#define REQUEST_THREAD_CREATE_SUCCESS 0
#define REQUEST_THREAD_CREATE_FAIL -1

// storage ////////////////////////////////////////////////////////////////////////////////////////
#define VECTORS_FOLDER "vectors/"
#define VECTOR_FILE_EXTENSION ".txt"
#define TEMP_VECTOR_FILE_EXTENSION ".tmp"

/*
    because this server is concurrent additional mechanisms must be applied to make sure, that
    different threads do not interrupt each other during data access, i.e. one thread could delete
    a vector while other thread reads. In order to prevent it each vector has it's own mutex 
    assigned. This struct contains all required information to make it working.
*/
struct vector_mutex {
    char vector_name[MAX_VECTOR_NAME_LEN];
    pthread_mutex_t mutex;
    int num_of_waiting_threads;
    int to_remove;
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// function declarations
///////////////////////////////////////////////////////////////////////////////////////////////////
/* 
    initializes server. 1 -> success, 0 -> fail
*/
int init();
/*
    creates and opens queues to listen for requests. 1 -> success, 0 -> fail
*/
int initialize_request_queues();
/*
    creates mutex for every stored vector. 1 -> success, 0 -> fail
*/
int initialize_vector_mutexes();
/*
    checks whether folder for storing vectors exists and if no then creates one
*/
int initialize_vectors_folder();
/*
    destroys all vector mutexes and frees memory after them. 1 -> success, 0 -> fail
*/
int destroy_vector_mutexes();
/*
    creates and adds a new vector mutex to the vector_mutexes list.
    1 --> success, 0 --> fail
*/
int add_vector_mutex(char* vec_name);
/*
    returns a pointer to the mutex for the vector with name equal to vector_name. Also increases
    the number of threads which are using the mutex. I keep track of it because on remove I must
    be sure that no other thread uses this mutex, because that could lead to an undefined behaviur
*/
struct vector_mutex* get_vector_mutex(char* vector_name);
/*
    calls pthread_mutex_unlock on the corresponding mutex and decreases the number of threads using
    the mutex. Also if mutex is signed as to_remove and no more threads use it then it is removed.
*/
int unlock_vector_mutex(struct vector_mutex* p_vec_mutex);
/*
    notify that the thread is not using this mutex anymore, but was not locked so do not unlock
*/
// int release_vector_mutex(struct vector_mutex* p_vec_mutex);
/*
    returns index of the mutex struct corresponding to the vector with name equal to vector_name
    in the vector_mutexes list. Returns -1 if not found, index otherwise
*/
int get_vector_mutex_idx(char* vector_name);
/*
    marks vector mutex to remove, so that no new threads can access it and it's removed when
    the last thread releases it
*/
int mark_vector_mutex_to_remove(struct vector_mutex* p_vec_mutex);
/*
    generic method for starting threads for requests. thread_function depends on queue from
    which server reads. Main thread waits till arguments are copied to new thread.
    REQUEST_THREAD_CREATION_SUCCESS -> success, REQUEST_THREAD_CREATION_FAIL -> fail
*/
int start_request_thread(void* (*thread_function)(void*), void* p_args);
/*
    set attributes and open a queue for vector initialization.
    returns:
    QUEUE_INIT_SUCCESS  if success
    QUEUE_OPEN_ERROR    if error occurred during opening the queue
*/
int initialize_init_vector_queue();
int initialize_set_queue();
int initialize_get_queue();
int initialize_destroy_queue();
/*
    closes and unlinks all the queues which were created for listening for requests.
    1 -> success, 0 -> fail
*/
int close_queues();
/*
    creates a vector physically
*/
int create_vector(char* name, int size);
/*
    copy message during new request thread creation. Is thread safe in sense that 
    it will notify the main thread that the message has been copied and the main thread 
    can proceed
*/
int copy_message(char* p_source, char* p_destination, int size);
/*
    performs logic for new vector initialization. Serves requests from the "init queue".
    Used as the function passed to request thread
*/
void* init_vector(void* p_init_msg);
/*
    performs logic for setting a value in a vector. Serves requests from the "set queue".
    Used as the function passed to request thread
*/
void* set(void* p_set_msg);
/*
    perfoms logic for getting a value form a vector. Servers requests from the "get queue".
    Used as the function passed to request thread
*/
void* get(void* p_get_msg);
/*
    performs logic for destroying a vector. Serves requests from the "destroy queue".
    Used as the function passed to request thread
*/
void* destroy(void* p_destroy_msg);
/* 
    starts a thread for reading user input. User input is readed in order to detect when
    server should stop so that all the clean up can be done
*/
int start_reading_user_input();
/*
    read user input and store it in user_input global variable
*/
void *update_user_input(void*);
/*
    create a file for a vector and initialize it with 0 values
*/
int create_array_file(char* name, int size);
/*
    returns size of a vector which is saved in a vector file. Requeres opening a file
*/
int get_vector_size(char* name);



///////////////////////////////////////////////////////////////////////////////////////////////////
// global variables
///////////////////////////////////////////////////////////////////////////////////////////////////
// general ////////////////////////////////////////////////////////////////////////////////////////
char user_input[] = INITIAL_COMMAND;  // for main loop finish detection

// request thread /////////////////////////////////////////////////////////////////////////////////
pthread_mutex_t mutex_msg;  // mutex used for waiting unitil request thread copies a message
int msg_not_copied = 1;     // flag used to check if a message has been copied by request thread
pthread_cond_t cond_msg;    // condition used together with mutex_msg for waiting until request 
                            // thread copies message
pthread_attr_t request_thread_attr;

pthread_mutex_t mutex_vec_mutex;    // mutex for acquiring and returning mutex for a particular
                                    // vector file

// user input /////////////////////////////////////////////////////////////////////////////////////
pthread_t user_input_thread;

// queue descriptors //////////////////////////////////////////////////////////////////////////////
mqd_t q_init_vector;    // queue for receiving requests to create a new vector
mqd_t q_set;            // queue for receiving requests to set a value in a vector
mqd_t q_get;            // queue for receiving requests to get a value from a vector
mqd_t q_destroy;        // queue for receiving requests to remove a vector

// storage ////////////////////////////////////////////////////////////////////////////////////////
struct vector_mutex** vector_mutexes;   // for each vector stores structs which conitain (beside
                                        // others) mutexes to access vector files



///////////////////////////////////////////////////////////////////////////////////////////////////
// main
///////////////////////////////////////////////////////////////////////////////////////////////////



int main (int argc, char **argv)
{
    printf("distributed vector server started\n");

    if (init() != 1)
    {
        perror("INIT could not initialize server");
        exit(1);
    }

    if (start_reading_user_input() == 0)
    {
        // messages to be retrived from requests
        struct init_msg in_init_msg;
        struct set_msg in_set_msg;
        struct get_msg in_get_msg;
        struct destroy_msg in_destroy_msg;

        // listen for requests till user wirtes exit command
        while (strcmp(user_input, EXIT_COMMAND) != 0)
        {
            // read messages in all queues if available
            if (mq_receive(q_init_vector, (char*) &in_init_msg, INIT_MSG_SIZE, NULL) != -1)
            {
                if (start_request_thread(init_vector, &in_init_msg) != REQUEST_THREAD_CREATE_SUCCESS)
                {
                    printf("REQUEST THREAD could not create thread for init vector request\n");
                }
            }

            if (mq_receive(q_set, (char*) &in_set_msg, SET_MSG_SIZE, NULL) != -1)
            {
                if (start_request_thread(set, &in_set_msg) != REQUEST_THREAD_CREATE_SUCCESS)
                {
                    printf("REQUEST THREAD could not create thread for set value request\n");
                }
            }

            if (mq_receive(q_get, (char*) &in_get_msg, GET_MSG_SIZE, NULL) != -1)
            {
                if (start_request_thread(get, &in_get_msg) != REQUEST_THREAD_CREATE_SUCCESS)
                {
                    printf("REQUEST THREAD could not create thread for get value request\n");
                }
            }

            if (mq_receive(q_destroy, (char*) &in_destroy_msg, DESTROY_MSG_SIZE, NULL) != -1)
            {
                if (start_request_thread(destroy, &in_destroy_msg) != REQUEST_THREAD_CREATE_SUCCESS)
                {
                    printf("REQUEST THREAD could not create thread for destroy request\n");
                }
            }
        } // end main while
    }
    else
    {
        printf("USER INPUT READ could not start reading for user input\n");
    }

    // clean up
    if (pthread_mutex_destroy(&mutex_msg) != 0)
        perror("CLEAN UP could not destroy mutex_msg");
    if (pthread_cond_destroy(&cond_msg) != 0)
        perror("CLEAN UP could not destroy cond_msg");
    if (pthread_attr_destroy(&request_thread_attr) != 0)
        perror("CLEAN UP could not destroy request_thread_attr");
    if (pthread_mutex_destroy(&mutex_vec_mutex) != 0)
        perror("CLEAN UP could not destroy mutex_vec_mutex");

    if (!destroy_vector_mutexes())
        printf("CLEAN UP could not destroy vector files mutexes\n");

    if (!close_queues())
        printf("CLEAN UP could not close queues\n");
}



int init()
{
    if (pthread_mutex_init(&mutex_msg, NULL) != 0)
    {
        perror("INIT could not init mutex_msg");
        return 0;
    }

    if (pthread_cond_init(&cond_msg, NULL) != 0)
    {
        perror("INIT could not init cond_msg");
        return 0;
    }

    if (pthread_attr_init(&request_thread_attr) != 0)
    {
        perror("INIT could not init request_thread_attr");
        return 0;
    }

    if (pthread_attr_setdetachstate(&request_thread_attr, PTHREAD_CREATE_DETACHED) != 0)
    {
        perror("INIT could not set detach state for request_thread_attr");
        return 0;
    }

    if (!initialize_vectors_folder())
    {
        printf("INIT could not initialize vectors folder\n");
        return 0;
    }

    if (!initialize_vector_mutexes())
    {
        printf("INIT coud not initialize vector mutexes\n");
        return 0;
    }

    if (!initialize_request_queues())
    {
        printf("INIT could not initialize request queues\n");
        return 0;
    }

    return 1;
}



int initialize_request_queues()
{
    // init queue
    if (initialize_init_vector_queue() != QUEUE_INIT_SUCCESS)
    {
        printf("INITIALIZE REQUEST QUEUES could not open init vector queue");
        return 0;
    }

    // set queue
    if (initialize_set_queue() != QUEUE_INIT_SUCCESS)
    {
        perror("INITIALIZE REQUEST QUEUES could not open set queue");
        return 0;
    }

    // get queue
    if (initialize_get_queue() != QUEUE_INIT_SUCCESS)
    {
        perror("INITIALIZE REQUEST QUEUES could not open get queue");
        return 0;
    }

    // destroy queue
    if (initialize_destroy_queue() != QUEUE_INIT_SUCCESS)
    {
        perror("INITIALIZE REQUEST QUEUES could not open destroy queue");
        return 0;
    }

    return 1;
}



int add_vector_mutex(char* vec_name)
{
    struct vector_mutex* p_vec_mut = (struct vector_mutex*) malloc(sizeof(struct vector_mutex));

    strcpy(p_vec_mut->vector_name, vec_name);
    p_vec_mut->num_of_waiting_threads = 0;
    p_vec_mut->to_remove = 0;

    if (pthread_mutex_init(&p_vec_mut->mutex, NULL) != 0)
    {
        printf("ADD VECTOR MUTEX could not initialize vector mutex\n");
        return 0;
    }

    vector_add(&vector_mutexes, p_vec_mut);

    return 1;
}



int initialize_vector_mutexes()
{
    int res = 1;

    DIR* vec_dir;
    struct dirent* vec_dir_ent;

    if ((vec_dir = opendir(VECTORS_FOLDER)) != NULL) // open the directory with vectors
    {
        vector_mutexes = vector_create();

        int extension_len = strlen(VECTOR_FILE_EXTENSION);      // length of vector file extension
        char extension[extension_len + 1];                      // vector file extension; + 1 because of \0
        extension[extension_len] = '\0';
        char f_name[MAX_VECTOR_NAME_LEN + extension_len + 1];   // vector file name with extension
        int f_name_len = 0;
        char f_name_no_extension[MAX_VECTOR_NAME_LEN];          // vector file name with no extension
        int f_name_no_extension_len = 0;

        while ((vec_dir_ent = readdir(vec_dir)) != NULL) // read all files in the vector directory
        {
            strcpy(f_name, vec_dir_ent->d_name);
            f_name_len = strlen(f_name);

            if (f_name_len > extension_len) // ignore non vector files (too short name)
            {
                // obtain file extension
                strncpy(extension, f_name + f_name_len - extension_len, extension_len);
                
                if (strcmp(extension, VECTOR_FILE_EXTENSION) == 0)  // ignore files with wrong extension
                {
                    f_name_no_extension_len = f_name_len - extension_len;
                    // cut just vector name - ignore file extension
                    strncpy(f_name_no_extension, f_name, f_name_no_extension_len);
                    // fininsh the f_name_no_extension with string end character
                    f_name_no_extension[f_name_no_extension_len] = '\0';
                    
                    if (!add_vector_mutex(f_name_no_extension))
                    {
                        res = 0;
                        printf("INITIALIZE VECTOR MUTEXES could not add the mutex to the list\n");
                    }
                }
            } // end if (f_name_len > extension_len)
        } // end while ((vec_dir_ent = readdir(vec_dir)) != NULL)

        if (closedir(vec_dir) != 0)
        {
            res = 0;
            perror("INITIALIZE VECTOR MUTEXES could not close vectors directory");
        }
    }
    else // couldn't open the vector's directory
    {
        res = 0;
        perror("INITIALIZE VECTOR MUTEXES could not open the vectors directory");
    }

    return res;
}



int initialize_vectors_folder()
{
    struct stat st = {0};

    // if the directory doesn't exist
    if (stat(VECTORS_FOLDER, &st) == -1)
    {
        if (mkdir(VECTORS_FOLDER, S_IRWXU) != 0)
        {
            perror("INITIALIZE VECTORS FOLDER could not create the vectors folder");
            return 0;
        }
    }

    return 1;
}



int destroy_vector_mutexes()
{
    int res = 1;

    int size = vector_size(vector_mutexes);
    for (int i = 0; i < size; i++)
    {
        if (pthread_mutex_destroy(&vector_mutexes[i]->mutex) != 0)
        {
            perror("DESTROY VECTOR MUTEXES cannot destroy mutex");
            printf("the mutex which could not be destroyed is for vector: %s\n", 
                vector_mutexes[i]->vector_name);
            
            res = 0;
        }

        free(vector_mutexes[i]);
    }

    vector_free(vector_mutexes);

    return res;
}



struct vector_mutex* get_vector_mutex(char* vector_name)
{
    struct vector_mutex* res = NULL;

    if (pthread_mutex_lock(&mutex_vec_mutex) == 0)
    {
        int size = vector_size(vector_mutexes);
        struct vector_mutex* p_vec_mutex = NULL;

        for (int i = 0; i < size; i++)
        {
            p_vec_mutex = vector_mutexes[i];

            if (strcmp(p_vec_mutex->vector_name, vector_name) == 0 &&
                !p_vec_mutex->to_remove)
            {
                vector_mutexes[i]->num_of_waiting_threads++;
                break;
            }

            p_vec_mutex = NULL;
        }
        
        if (pthread_mutex_unlock(&mutex_vec_mutex) == 0)
            res = p_vec_mutex;
        else
        {
            if (p_vec_mutex != NULL)
                p_vec_mutex->num_of_waiting_threads--;
            perror("GET VECTOR MUTEX could not unlock mutex");
        }        
    }
    else // couldn't lock mutex_vec_mutex
        perror("GET VECTOR MUTEX could not lock mutex_vec_mutex");

    return res;
}



int unlock_vector_mutex(struct vector_mutex* p_vector_mutex)
{
    int res = 1;

    if (pthread_mutex_lock(&mutex_vec_mutex) == 0)
    {
        int initial_num_of_waiting_threads = p_vector_mutex->num_of_waiting_threads;

        p_vector_mutex->num_of_waiting_threads--;

        // if marked to remove and no more threads are waiting remove it and free space
        if (p_vector_mutex->to_remove == 1 && p_vector_mutex->num_of_waiting_threads == 0)
        {
            int size = vector_size(vector_mutexes);
            for (int i = 0; i < size; i++)
            {
                if (vector_mutexes[i] == p_vector_mutex)
                {
                    if (pthread_mutex_unlock(&p_vector_mutex->mutex) == 0)
                    {
                        vector_remove(vector_mutexes, i);
                        free(p_vector_mutex);
                    }
                    else
                    {
                        res = 0;
                        p_vector_mutex->num_of_waiting_threads = initial_num_of_waiting_threads;
                        perror("UNLOCK VECTOR MUTEX could not unlock the requested mutex");
                    }
                }
            }
        }
        else
        {
            if (pthread_mutex_unlock(&p_vector_mutex->mutex) != 0)
            {
                res = 0;
                p_vector_mutex->num_of_waiting_threads = initial_num_of_waiting_threads;
                perror("UNLOCK VECTOR MUTEX could not unlock the requested mutex, no remove");
            }
        }
        

        if (pthread_mutex_unlock(&mutex_vec_mutex) != 0)
        {
            res = 0;
            p_vector_mutex->num_of_waiting_threads = initial_num_of_waiting_threads;
            perror("UNLOCK VECTOR MUTEX could not unlock mutex_vec_mutex");
        }
    }
    else // couldn't lock mutex_vec_mutex
        perror("UNLOCK VECTOR MUTEX could not lock mutex_vec_mutex");

    return res;
}



int get_vector_mutex_idx(char* vector_name)
{
    int size = vector_size(vector_mutexes);
    for (int i = 0; i < size; i++)
    {
        if (strcmp(vector_mutexes[i]->vector_name, vector_name) == 0)
        {
            return i;
        }
    }

    return -1;
}



int mark_vector_mutex_to_remove(struct vector_mutex* p_vector_mutex)
{
    int res = 1;
    
    if (pthread_mutex_lock(&mutex_vec_mutex) == 0)
    {
        p_vector_mutex->to_remove = 1;

        if (pthread_mutex_unlock(&mutex_vec_mutex) != 0)
        {
            res = 0;
            perror("MARK VECTOR MUTEX TO REMOVE could not unlock mutex");
        }
    }
    else
    {
        perror("MARK VECTOR MUTEX TO REMOVE could not lock mutex");
        res = 0;
    }
    
    return res;
}



int close_queues()
{
    int res = 1;

    // close init vector queue
    if (mq_close(q_init_vector) != 0)
    {
        perror("CLEAN UP could not close inint vector queue");
        res = 0;
    }
    if (mq_unlink(INIT_VECTOR_QUEUE_NAME) != 0)
    {
        perror("CLEAN UP could not unlink init vector queue");
        res = 0;
    }

    // close set queue
    if (mq_close(q_set) != 0)
    {
        perror("CLEAN UP could not close set queue");
        res = 0;
    }
    if (mq_unlink(SET_QUEUE_NAME) != 0)
    {
        perror("CLEAN UP could not unlink set queue");
        res = 0;
    }

    // close get queue
    if (mq_close(q_get) != 0)
    {
        perror("CLEAN UP could not close get queue");
        res = 0;
    }
    if (mq_unlink(GET_QUEUE_NAME) != 0)
    {
        perror("CLEAN UP could not unlink get queue");
        res = 0;
    }

    // close destroy queue
    if (mq_close(q_destroy) != 0)
    {
        perror("CLEAN UP could not close destroy queue");
        res = 0;
    }
    if (mq_unlink(DESTROY_QUEUE_NAME) != 0)
    {
        perror("CLEAN UP could not unlink destroy queue");
        res = 0;
    }

    return res;
}



///////////////////////////////////////////////////////////////////////////////////////////////////
// request threads
///////////////////////////////////////////////////////////////////////////////////////////////////



int start_request_thread(void* (*thread_function)(void*), void* p_args)
{
    int res = REQUEST_THREAD_CREATE_SUCCESS;
    pthread_t th_id;
    if (pthread_create(&th_id, &request_thread_attr, thread_function, p_args) != 0)
    {
        perror("START REQUEST THREAD could not create the thread");
        res = REQUEST_THREAD_CREATE_FAIL;
    }
    else // thread created
    {
        // wait until thread copies message
        if (pthread_mutex_lock(&mutex_msg) == 0)
        {
            while (msg_not_copied)
            {
                if (pthread_cond_wait(&cond_msg, &mutex_msg) != 0)
                {
                    perror("START REQUEST THREAD could not wait for the condition");
                    res = REQUEST_THREAD_CREATE_FAIL;
                    break;
                }
            }
            msg_not_copied = 1; // thread changed it to 0 after copying message, change it to initial state
            if (pthread_mutex_unlock(&mutex_msg) != 0)
            {
                perror("START REQUEST THREAD could not unlock the mutex_msg");
                res = REQUEST_THREAD_CREATE_FAIL;
            }
        }
        else // couldn't lock mutex
        {
            perror("START REQUEST THREAD could not lock the mutex_msg");
            res = REQUEST_THREAD_CREATE_FAIL;
        }
    }

    return res;
}



int copy_message(char* p_source, char* p_destination, int size)
{
    int res = 1;
    if (pthread_mutex_lock(&mutex_msg) == 0)
    {
        memcpy(p_destination, p_source, size);   // copy message to local variable
        msg_not_copied = 0;                      // info for main thread to proceed
        if (pthread_cond_signal(&cond_msg) == 0)
        {
            if (pthread_mutex_unlock(&mutex_msg) != 0)
            {
                perror("COPY MESSAGE could not unlock the mutex_msg");
                res = 0;
            }
        }
        else
        {
            perror("COPY MESSAGE could not signal");
            res = 0;
        }               
    }     
    else // couldn't lock mutex
    {
        perror("COPY MESSAGE could not lock the mutex_msg");
        res = 0;
    }

    return res;                         
}



///////////////////////////////////////////////////////////////////////////////////////////////////
// init vector functions
///////////////////////////////////////////////////////////////////////////////////////////////////



int initialize_init_vector_queue()
{
    int res = QUEUE_INIT_SUCCESS;

    struct mq_attr q_init_vector_attr;
    
    q_init_vector_attr.mq_flags = 0;                                // ingnored for MQ_OPEN
    q_init_vector_attr.mq_maxmsg = INIT_VECTOR_QUEUE_MAX_MESSAGES;
    q_init_vector_attr.mq_msgsize = INIT_MSG_SIZE;        
    q_init_vector_attr.mq_curmsgs = 0;                              // initially 0 messages

    int open_flags = O_CREAT | O_RDONLY | O_NONBLOCK;
    mode_t permissions = S_IRUSR | S_IWUSR;                         // allow reads and writes into queue

    if ((
        q_init_vector = mq_open(INIT_VECTOR_QUEUE_NAME, open_flags, permissions, 
        &q_init_vector_attr)) == -1)
    {
        perror("INITIALIZE INIT VECTOR QUEEU could not open the queue");
        res = QUEUE_OPEN_ERROR;
    }
    
    return res;
}



void *init_vector(void* p_init_msg)
{
    struct init_msg init_msg;
    if (copy_message((char*) p_init_msg, (char*) &init_msg, INIT_MSG_SIZE) == 1)
    {
        // create vector
        int response = create_vector(init_msg.name, init_msg.size);
        
        // send response
        mqd_t q_resp;
        if ((q_resp = mq_open(init_msg.resp_queue_name, O_WRONLY)) == -1)
        {
            perror("RESPONSE ERROR could not open queue for sending response");
        }
        else
        {
            if (mq_send(q_resp, (char*) &response, sizeof(int), 0) == -1)
            {
                perror("RESPONSE ERROR could not send response");
            }

            if (mq_close(q_resp) == -1)
            {
                perror ("RESPONSE QUEUE could not close response queue");
            }
        }
    }
    else
    {
        printf("INIT VECTOR couldn't copy message\n");
    }
    
    pthread_exit(0);
}



int create_vector(char* name, int size)
{
    int res = NEW_VECTOR_CREATED;

    int old_vec_size = get_vector_size(name);
    
    if (old_vec_size < 0) // vector doesn't exist
    {
        if (create_array_file(name, size))
            res = NEW_VECTOR_CREATED;
        else
        {
            res = VECTOR_CREATION_ERROR;
            printf("CREATE VECTOR could not create vector\n");
        }
    }
    else if (old_vec_size == size)
        res = VECTOR_ALREADY_EXISTS;
    else
        res = VECTOR_CREATION_ERROR;
    
    return res;
}



void get_full_vector_file_name(char* file_name, char* vector_name)
{
    strcpy(file_name, VECTORS_FOLDER);
    strcat(file_name, vector_name);
    strcat(file_name, VECTOR_FILE_EXTENSION);
}



int get_full_vector_file_name_max_len()
{
    return MAX_VECTOR_NAME_LEN + strlen(VECTOR_FILE_EXTENSION) + strlen(VECTORS_FOLDER) + 1;
}



int get_vector_size(char* name)
{
    int len = -1;

    char file_name[get_full_vector_file_name_max_len()];
    get_full_vector_file_name(file_name, name);

    FILE* fp;
    fp = fopen(file_name, "r");

    if (fp != NULL)
    {
        char len_str[20];
        if (fgets(len_str, 20, fp) != NULL)
        {
            if (sscanf(len_str, "%d", &len) != 1)
            {
                printf("GET VECTOR SIZE vector file wrong format");
                len = -1;
            }
        }
        else
        {
            len = -1;
        }
        
        if (fclose(fp) != 0)
        {
            len = -1;
        }
    }
    else
    {
        len = -1;
    }
    
    return len;
}



int initialize_array_file(FILE* fp, int size)
{
    int res = 1;

    if (fprintf(fp, "%d\n", size) > 0)
    {
        for (int i = 0; i < size; i++)
        {
            if (fprintf(fp, "%d\n", 0) < 0)
            {
                res = 0;
                break;
            }
        }
    }
    else
    {
        res = 0;
    }

    return res;
}



int create_array_file(char* name, int size)
{
    int res = 1;
    struct vector_mutex* p_vec_mutex = NULL;
    pthread_mutex_t* p_vec_file_mutex = NULL;

    if (add_vector_mutex(name)) // create mutex for new vector
    {
        p_vec_mutex = get_vector_mutex(name); // acquire newly created mutex
        
        if (p_vec_mutex != NULL)
        {
            p_vec_file_mutex = &p_vec_mutex->mutex;

            if (pthread_mutex_lock(p_vec_file_mutex) == 0)  // lock access to vector file
            {
                char file_name[get_full_vector_file_name_max_len()];
                get_full_vector_file_name(file_name, name);
            
                FILE* fp;
                fp = fopen(file_name, "w");

                if (fp != NULL)
                {
                    if (!initialize_array_file(fp, size))
                    {
                        res = 0;
                        printf("CREATE ARRAY FILE could not initialize file\n");
                    }
                    
                    if (fclose(fp) != 0)
                    {
                        res = 0;
                        perror("CREATE ARRAY FILE could not close file descriptor");
                    }

                    if (!unlock_vector_mutex(p_vec_mutex))
                    {
                        res = 0;
                        perror("CREATE ARRAY FILE could not unlock mutex");
                    }
                }
                else // NULL vector file pointer
                {
                    res = 0;
                    printf("CREATE ARRAY FILE null vector file pointer\n");
                }
            }
            else // couldn't lock mutex
            {
                res = 0;
                perror("CREATE ARRAY FILE could not lock mutex");
            }
        }
        else // NULL vector mutex, no such vector
        {
            res = 0;
        }
    }
    else // couldn't create vector mutex
    {
        res = 0;
        printf("CREATE ARRAY FILE could not create vector mutex\n");
    }

    // in case mutex was created but some other errors occurred remove the mutex
    if (res == 0 && p_vec_file_mutex != NULL)
    {
        vector_remove(vector_mutexes, vector_size(vector_mutexes) - 1);
    }
    
    return res;
}



///////////////////////////////////////////////////////////////////////////////////////////////////
// set value in vector functions
///////////////////////////////////////////////////////////////////////////////////////////////////



int initialize_set_queue()
{
    int res = QUEUE_INIT_SUCCESS;

    struct mq_attr q_set_attr;
    
    q_set_attr.mq_flags = 0;                                // ingnored for MQ_OPEN
    q_set_attr.mq_maxmsg = SET_QUEUE_MAX_MESSAGES;
    q_set_attr.mq_msgsize = SET_MSG_SIZE;        
    q_set_attr.mq_curmsgs = 0;                              // initially 0 messages

    int open_flags = O_CREAT | O_RDONLY | O_NONBLOCK;
    mode_t permissions = S_IRUSR | S_IWUSR;                 // allow reads and writes into queue

    if ((
        q_set = mq_open(SET_QUEUE_NAME, open_flags, permissions, 
        &q_set_attr)) == -1)
    {
        perror("INITIALIZE SET QUEUE could not open the queue");
        res = QUEUE_OPEN_ERROR;
    }
    
    return res;
}



int set_value_in_vector_file(char* vec_name, int pos, int val)
{
    if (pos < 0)
        return SET_FAIL;

    int res = SET_SUCCESS;
    int value_changed = 0;  // 1 if vaule is changed at pos

    int max_full_vector_file_name_len = get_full_vector_file_name_max_len();
    char full_vector_file_name[max_full_vector_file_name_len];
    get_full_vector_file_name(full_vector_file_name, vec_name);
    
    // name for temporal file with changed value, the same that original but *.tmp
    char temp_file_name[max_full_vector_file_name_len];
    strncpy(temp_file_name, 
        full_vector_file_name, strlen(full_vector_file_name) - strlen(VECTOR_FILE_EXTENSION));
    strcat(temp_file_name, TEMP_VECTOR_FILE_EXTENSION);
    
    struct vector_mutex* p_vec_mutex = NULL;
    pthread_mutex_t* p_mutex_vec_file = NULL;

    if ((p_vec_mutex = get_vector_mutex(vec_name)) != NULL) // obtain mutex for the vector file
    {
        p_mutex_vec_file = &p_vec_mutex->mutex;
        if (pthread_mutex_lock(p_mutex_vec_file) == 0)    // lock mutex for the vector file
        {
            FILE* p_old, *p_new;

            if ((p_old = fopen(full_vector_file_name, "r")) != NULL)    // open current vector file
            {
                if ((p_new = fopen(temp_file_name, "w")) != NULL)       // open temporal vector file
                {
                    char line[20];      // buffer for reading old file
                    int line_idx = -1;  // -1 because first line is size of vector
                    while(fgets(line, 20, p_old) != NULL)
                    {
                        if (line_idx != pos) // not specified position
                        {
                            if (fputs(line, p_new) < 0) // copy from old to new file
                            {
                                perror("SET VALUE IN VECTOR FILE could not wirte into temp file");
                                res = SET_FAIL;
                                break;
                            }
                        }
                        else // reached required position
                        {
                            if (fprintf(p_new, "%d\n", val) < 0)    // print new value
                            {
                                perror("SET VALUE IN VECTOR FILE could not wirte wanted number into temp file");
                                res = SET_FAIL;
                                break;
                            }
                            else // value changed
                                value_changed = 1;
                        }

                        line_idx++;
                    }

                    if (fclose(p_old) == 0)
                    {
                        if (fclose(p_new) == 0)
                        {
                            if (remove(full_vector_file_name) == 0)
                            {
                                if (rename(temp_file_name, full_vector_file_name) != 0)
                                {
                                    res = SET_FAIL;
                                    perror("SET_VALUE_IN_VECTOR_FILE could not rename new file");
                                }
                            }
                            else // couldn't remove old file
                            {
                                res = SET_FAIL;
                                perror("SET_VALUE_IN_VECTOR_FILE could not remove old file");
                            }
                        }
                        else // couldn't close tmp file
                        {
                            res = SET_FAIL;
                            perror("SET_VALUE_IN_VECTOR_FILE could not close temp file");
                        }
                    }
                    else // couldn't close old file
                    {
                        res = SET_FAIL;
                        perror("SET_VALUE_IN_VECTOR_FILE could not close old file");
                    }
                }
                else // can't create temporary vector file
                {
                    res = SET_FAIL;  
                    perror("SET VALUE IN VECTOR FILE could not create temporal file");
                    if (fclose(p_old) != 0)
                        perror("SET VALUE IN VECTOR FILE could not close vector file after creating temporal file failed");
                }  
            }
            else // can't open old vector file
            {
                res = SET_FAIL;   
                perror("SET VALUE IN VECTOR FILE could not open the vector file");
            }

            if (!unlock_vector_mutex(p_vec_mutex))
                res = SET_FAIL;      
        }
        else // can't lock mutex
        {
            res = SET_FAIL;
            perror("SET VALUE IN VECTOR FILE could not lock the mutex");
        }
    }
    else // can't obtain mutex, no such vector
    {
        res = SET_FAIL;
    }

    if (res == SET_SUCCESS && value_changed == 1)
        return SET_SUCCESS;
    else
        return SET_FAIL;
}



void *set(void* p_set_msg)
{
    struct set_msg set_msg;
    if (copy_message((char*) p_set_msg, (char*) &set_msg, SET_MSG_SIZE) == 1)
    {
        // set value in file
        int response = set_value_in_vector_file(set_msg.name, set_msg.pos, set_msg.value);
        
        // send response
        mqd_t q_resp;
        if ((q_resp = mq_open(set_msg.resp_queue_name, O_WRONLY)) == -1)
        {
            perror("RESPONSE ERROR could not open queue for sending response");
        }
        else
        {
            if (mq_send(q_resp, (char*) &response, sizeof(int), 0) == -1)
            {
                perror("RESPONSE ERROR could not send response");
            }

            if (mq_close(q_resp) == -1)
            {
                perror ("RESPONSE QUEUE could not close response queue");
            }
        }
    }
    else
    {
        printf("SET couldn't copy_message\n");
    }
    
    pthread_exit(0);
}



///////////////////////////////////////////////////////////////////////////////////////////////////
// get value from vector
///////////////////////////////////////////////////////////////////////////////////////////////////



int initialize_get_queue()
{
    int res = QUEUE_INIT_SUCCESS;

    struct mq_attr q_get_attr;
    
    q_get_attr.mq_flags = 0;                                // ingnored for MQ_OPEN
    q_get_attr.mq_maxmsg = GET_QUEUE_MAX_MESSAGES;
    q_get_attr.mq_msgsize = GET_MSG_SIZE;        
    q_get_attr.mq_curmsgs = 0;                              // initially 0 messages

    int open_flags = O_CREAT | O_RDONLY | O_NONBLOCK;
    mode_t permissions = S_IRUSR | S_IWUSR;                 // allow reads and writes into queue

    if ((
        q_get = mq_open(GET_QUEUE_NAME, open_flags, permissions, 
        &q_get_attr)) == -1)
    {
        perror("INITIALIZE GET QUEUE could not open the queue");
        res = QUEUE_OPEN_ERROR;
    }
    
    return res;
}



int get_value_from_vector_file(char* vec_name, int pos, int* p_value)
{
    if (pos < 0)
        return 0;

    int res = 1;
    int found = 0;

    struct vector_mutex* p_vec_mutex;
    pthread_mutex_t* p_mutex_vec;
    if ((p_vec_mutex = get_vector_mutex(vec_name)) != NULL) // obtain mutex for the vector file
    {
        p_mutex_vec = &p_vec_mutex->mutex;
        if (pthread_mutex_lock(p_mutex_vec) == 0) // lock vector file mutex
        {
            char full_vector_file_name[get_full_vector_file_name_max_len()];
            get_full_vector_file_name(full_vector_file_name, vec_name);

            FILE* fp;

            if ((fp = fopen(full_vector_file_name, "r")) != NULL) // open the vector file
            {
                char line[20];      // temp varible for reading lines from vector file
                int line_idx = -1;  // -1 because line 0 is size of vector
                while (fgets(line, 20, fp) != NULL)
                {
                    if (line_idx == pos)
                    {
                        found = 1;
                        if (sscanf(line, "%d", p_value) != 1)
                        {
                            perror("GET VALUE FROM VECTOR FILE could not match value to integer");
                            res = 0;
                        }

                        break;
                    }

                    line_idx++;
                } // end read file while

                if (fclose(fp) != 0)
                {
                    res = 0;
                    perror("GET VALUE FROM VECTOR FILE could not close file");
                }
            }
            else // can't open file
            {
                res = 0;
                perror("GET VALUE FROM VECTOR FILE could not open file");
            }

            if (!unlock_vector_mutex(p_vec_mutex))
            {
                res = 0;
                perror("GET VALUE FROM VECTOR FILE could not unlock mutex");
            }
        }
        else // couldn't lock mutex
        {
            res = 0;
            perror("GET VALUE FROM VECTOR FILE could not lock mutex");
        }  
    }
    else // vector doesn't exist
    {
        res = 0;
    }

    return res && found;
}



void* get(void* p_get_msg)
{
    struct get_msg get_msg;
    if (copy_message((char*) p_get_msg, (char*) &get_msg, GET_MSG_SIZE) == 1)
    {
        // get value from file
        int value = 0;
        int error = get_value_from_vector_file(get_msg.name, get_msg.pos, &value) ? 
            GET_SUCCESS : GET_FAIL;
        
        // send response
        mqd_t q_resp;
        if ((q_resp = mq_open(get_msg.resp_queue_name, O_WRONLY)) == -1)
        {
            perror("RESPONSE ERROR could not open queue for sending response");
        }
        else
        {
            struct get_resp_msg response;
            response.error = error;
            response.value = value;

            if (mq_send(q_resp, (char*) &response, GET_RESP_MSG_SIZE, 0) == -1)
            {
                perror("RESPONSE ERROR could not send response");
            }

            if (mq_close(q_resp) == -1)
            {
                perror ("RESPONSE QUEUE could not close response queue");
            }
        }
    }
    else
    {
        printf("GET couldn't copy_message\n");
    }
    
    pthread_exit(0);
}



///////////////////////////////////////////////////////////////////////////////////////////////////
// destroy
///////////////////////////////////////////////////////////////////////////////////////////////////



int initialize_destroy_queue()
{
    int res = QUEUE_INIT_SUCCESS;

    struct mq_attr q_destroy_attr;
    
    q_destroy_attr.mq_flags = 0;                                // ingnored for MQ_OPEN
    q_destroy_attr.mq_maxmsg = DESTROY_QUEUE_MAX_MESSAGES;
    q_destroy_attr.mq_msgsize = DESTROY_MSG_SIZE;        
    q_destroy_attr.mq_curmsgs = 0;                              // initially 0 messages

    int open_flags = O_CREAT | O_RDONLY | O_NONBLOCK;
    mode_t permissions = S_IRUSR | S_IWUSR;                 // allow reads and writes into queue

    if ((
        q_destroy = mq_open(DESTROY_QUEUE_NAME, open_flags, permissions, 
        &q_destroy_attr)) == -1)
    {
        perror("INITIALIZE DESTROY QUEUE could not open the queue");
        res = QUEUE_OPEN_ERROR;
    }
    
    return res;
}



void* destroy(void* p_destroy_msg)
{
    struct destroy_msg destroy_msg;
    if (copy_message((char*) p_destroy_msg, (char*) &destroy_msg, DESTROY_MSG_SIZE) == 1)
    {
        int result = DESTROY_SUCCESS;
        
        struct vector_mutex* p_vec_mutex;
        pthread_mutex_t* p_mutex_vec;
        
        if ((p_vec_mutex = get_vector_mutex(destroy_msg.name)) != NULL)
        {
            p_mutex_vec = &p_vec_mutex->mutex;
            char full_vector_file_name[get_full_vector_file_name_max_len()];
            get_full_vector_file_name(full_vector_file_name, destroy_msg.name);
            
            if (pthread_mutex_lock(p_mutex_vec) == 0)
            {
                if (remove(full_vector_file_name) != 0) // if couldn't remove the file
                {
                    perror("DESTROY could not remove the vector file");
                    result = DESTROY_FAIL;
                }

                if (mark_vector_mutex_to_remove(p_vec_mutex))
                {
                    if (!unlock_vector_mutex(p_vec_mutex))
                    {
                        result = DESTROY_FAIL;
                        printf("DESTROY could not unlock vector mutex");
                    }
                }
                else
                {
                    printf("DESTROY could not set mutex to remove\n");
                }
                
            }
            else // couldn't lock mutex
            {
                result = DESTROY_FAIL;
                perror("DESTROY could not lock the mutex");
            }
        }
        else // vector doesn't exist
        {
            result = DESTROY_FAIL;
        }
        
        // send response
        mqd_t q_resp;
        if ((q_resp = mq_open(destroy_msg.resp_queue_name, O_WRONLY)) == -1)
        {
            perror("RESPONSE ERROR could not open queue for sending response");
        }
        else
        {
            if (mq_send(q_resp, (char*) &result, sizeof(int), 0) == -1)
            {
                perror("RESPONSE ERROR could not send response");
            }

            if (mq_close(q_resp) == -1)
            {
                perror ("RESPONSE QUEUE could not close response queue");
            }
        }
    }
    else
    {
        printf("DESTROY couldn't copy_message\n");
    }
    
    pthread_exit(0);
}



///////////////////////////////////////////////////////////////////////////////////////////////////
// unblocked user input read
///////////////////////////////////////////////////////////////////////////////////////////////////



int start_reading_user_input()
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    
    int res = pthread_create(&user_input_thread, &attr, update_user_input, NULL);

    if (pthread_attr_destroy(&attr) != 0)
    {
        res = -1;
        perror("START READING USER INPUT could not destroy attributes");
    }

    return res;
}



void *update_user_input(void* arg)
{
    char* res = NULL;

    while(strcmp(user_input, EXIT_COMMAND) != 0)
    {
        res = fgets(user_input, 2, stdin);
        if (res == NULL)
        {
            perror("USER INPUT error during reading user input");
        }
    }

    pthread_exit(0);
}



