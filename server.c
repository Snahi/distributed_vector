#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <unistd.h>         // for unblock read function
#include <pthread.h>



///////////////////////////////////////////////////////////////////////////////////////////////////
// const
///////////////////////////////////////////////////////////////////////////////////////////////////
// storage
#define VECTORS_FOLDER "vectors/"
#define VECTOR_FILE_EXTENSION ".txt"

// user input
#define INITIAL_COMMAND "c"
#define EXIT_COMMAND "q"

// init vector
#define NEW_VECTOR_CREATED 1
#define VECTOR_ALREADY_EXISTS 0
#define VECTOR_CREATION_ERROR -1

// errors
#define QUEUE_OPEN_ERROR 13
#define QUEUE_INIT_SUCCESS 1
#define REQUEST_THREAD_CREATE_SUCCESS 0
#define REQUEST_THREAD_CREATE_FAIL -1

// init
#define INIT_VECTOR_QUEUE_NAME "/init"
#define INIT_VECTOR_QUEUE_MAX_MESSAGES 10
#define MAX_VECTOR_NAME_LEN 39
#define MAX_RESP_QUEUE_NAME_LEN 64

struct init_msg {
    char name[MAX_VECTOR_NAME_LEN];
    int size;
    char resp_queue_name[MAX_RESP_QUEUE_NAME_LEN];
};

#define INIT_MSG_SIZE sizeof(struct init_msg)



///////////////////////////////////////////////////////////////////////////////////////////////////
// function declarations
///////////////////////////////////////////////////////////////////////////////////////////////////



/* 
    initializes server. 1 -> success
*/
int init();
/*
    creates and opens queues to listen for requests. 1 -> success
*/
int initialize_request_queues();
/*
    generic method for starting threads for requests. thread_function depends on queue from
    which server reads. Main thread waits till arguments are copied to new thread.
*/
int start_request_thread(void* (*thread_function)(void*), void* p_args);
/*
    set attributes and open a queue for vector initialization.
    returns:
    QUEUE_INIT_SUCCESS  if success
    QUEUE_OPEN_ERROR    if error occurred during opening the queue
*/
int initialize_init_vector_queue();
/*
    creat a vector physically
*/
int create_vector(char* name, int size);
int copy_message(char* p_source, char* p_destination, int size);
void *init_vector(void* p_init_msg);
/* 
    starts a thread for reading user input
*/
int start_reading_user_input();
/*
    read user input and store it in user_input global variable
*/
void *update_user_input(void*);
int create_array_file(char* name, int size);
int get_vector_size(char* name);



///////////////////////////////////////////////////////////////////////////////////////////////////
// global variables
///////////////////////////////////////////////////////////////////////////////////////////////////
// general
char user_input[] = INITIAL_COMMAND;  // for program end detection

// request thread
pthread_mutex_t mutex_msg;  // mutex used for waiting unitil request thread copies a message
int msg_not_copied = 1;     // flag used to check if a message has been copied by request thread
pthread_cond_t cond_msg;    // condition used together with mutex_msg for waiting until request 
                            // thread copies message
pthread_attr_t request_thread_attr;

// user input
pthread_t user_input_thread;

// queue descriptors
mqd_t q_init_vector;

// storage
int** vectors;



///////////////////////////////////////////////////////////////////////////////////////////////////
// main
///////////////////////////////////////////////////////////////////////////////////////////////////



int main (int argc, char **argv)
{
    printf("distributed vector server started\n");

    if (init() != 1)
    {
        exit(1);
    }

    if (start_reading_user_input() == 0)
    {
        // messages
        struct init_msg in_init_msg;

        // listen for requests
        while (strcmp(user_input, EXIT_COMMAND) != 0)
        {
            // read messages in all queues if available
            if (mq_receive(q_init_vector, (char*) &in_init_msg, INIT_MSG_SIZE, NULL) != -1)
            {
                // init queue
                if (start_request_thread(init_vector, &in_init_msg) != REQUEST_THREAD_CREATE_SUCCESS)
                {
                    perror("REQUEST THREAD could not create thread for request");
                }
            }
        }
    }
    else
    {
        perror("USER INPUT READ could not start reading for user input");
    }

    // clean up
    if (pthread_mutex_destroy(&mutex_msg) != 0)
        perror("CLEAN UP could not destroy mutex");
    if (pthread_cond_destroy(&cond_msg) != 0)
        perror("CLEAN UP could not destroy conditional variable");
    if (pthread_attr_destroy(&request_thread_attr) != 0)
        perror("CLEAN UP could not destroy pthread attributes");

    // close queues
    if (mq_close(q_init_vector) != 0)
        perror("CLEAN UP could not close inint vector queue");
    if (mq_unlink(INIT_VECTOR_QUEUE_NAME) != 0)
        perror("CLEAN UP could not unlink init vector queue");
}



int init()
{
    if (pthread_mutex_init(&mutex_msg, NULL) != 0)
        return 0;
    if (pthread_cond_init(&cond_msg, NULL) != 0)
        return 0;
    if (pthread_attr_init(&request_thread_attr) != 0)
        return 0;
    if (pthread_attr_setdetachstate(&request_thread_attr, PTHREAD_CREATE_DETACHED) != 0)
        return 0;

    return initialize_request_queues();
}



int initialize_request_queues()
{
    // init queue
    if (initialize_init_vector_queue() != QUEUE_INIT_SUCCESS)
    {
        perror("INIT ERROR: could not open init vector queue");
        return 0;
    }

    return 1;
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
                    res = REQUEST_THREAD_CREATE_FAIL;
                    break;
                }
            }
            msg_not_copied = 1; // thread changed it to 0 after copying message, change it to initial state
            if (pthread_mutex_unlock(&mutex_msg) != 0)
            {
                res = REQUEST_THREAD_CREATE_FAIL;
            }
        }
        else // couldn't lock mutex
        {
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
                res = 0;
            }
        }
        else
        {
            res = 0;
        }               
    }     
    else
    {
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
            perror("RESPONSE ERROR could not open queue for sending response to init vector");
        }
        else
        {
            if (mq_send(q_resp, (char*) &response, sizeof(int), 0) == -1)
            {
                perror("RESPONSE ERROR could not send response to init vector");
            }

            if (mq_close(q_resp) == -1)
            {
                perror ("RESPONSE QUEUE could not close response queue");
            }
        }
    }
    else
    {
        perror("INIT VECTOR couldn't copy_message");
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
            res = VECTOR_CREATION_ERROR;
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
    return MAX_VECTOR_NAME_LEN + strlen(VECTOR_FILE_EXTENSION) + 1;
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
            sscanf(len_str, "%d", &len);
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



int create_array_file(char* name, int size)
{
    int res = 1;

    char file_name[get_full_vector_file_name_max_len()];
    get_full_vector_file_name(file_name, name);
   
    FILE* fp;
    fp = fopen(file_name, "w");

    if (fp != NULL)
    {
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
        
        if (fclose(fp) != 0)
        {
            res = 0;
        }
    }
    else
    {
        res = 0;
    }
    
    return res;
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



