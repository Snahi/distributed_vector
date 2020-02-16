#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <unistd.h>         // for unblock read function
#include <pthread.h>


// const
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
#define MAX_VECTOR_NAME_LEN 40
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
    set attributes and open a queue for vector initialization.
    returns:
    QUEUE_INIT_SUCCESS  if success
    QUEUE_OPEN_ERROR    if error occurred during opening the queue
*/
int initialize_init_vector_queue();
int start_init_vector_thread(struct init_msg* p_init_msg);
/*
    read user input withoug blocking current thread
*/
void *init_vector(void* p_init_msg);
int start_reading_user_input();
void *update_user_input(void*);



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



///////////////////////////////////////////////////////////////////////////////////////////////////
// main
///////////////////////////////////////////////////////////////////////////////////////////////////



int main (int argc, char **argv)
{
    printf("distributed vector server started\n");

    pthread_mutex_init(&mutex_msg, NULL);
    pthread_cond_init(&cond_msg, NULL);
    pthread_attr_init(&request_thread_attr);
    pthread_attr_setdetachstate(&request_thread_attr, PTHREAD_CREATE_DETACHED);

    if (initialize_init_vector_queue() != QUEUE_INIT_SUCCESS)
    {
        perror("INIT ERROR: could not open init vector queue");
        exit(-1);
    }

    if (start_reading_user_input() == 0)
    {
        // messages
        struct init_msg in_init_msg;

        // listen for requests

        while (strcmp(user_input, EXIT_COMMAND) != 0)
        {
            // read messages in all queues
            if (mq_receive(q_init_vector, (char*) &in_init_msg, INIT_MSG_SIZE, NULL) != -1)
            {
                if (start_init_vector_thread(&in_init_msg) != REQUEST_THREAD_CREATE_SUCCESS)
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
    pthread_mutex_destroy(&mutex_msg);
    pthread_cond_destroy(&cond_msg);
    pthread_attr_destroy(&request_thread_attr);

    // close queues
    mq_close(q_init_vector);
    mq_unlink(INIT_VECTOR_QUEUE_NAME);
}



///////////////////////////////////////////////////////////////////////////////////////////////////
// init vector functions
///////////////////////////////////////////////////////////////////////////////////////////////////



int initialize_init_vector_queue()
{
    int res = QUEUE_INIT_SUCCESS;

    struct mq_attr q_init_vector_attr;
    
    q_init_vector_attr.mq_flags = O_NONBLOCK;                       // cannot wait, because there are also other requests
    q_init_vector_attr.mq_maxmsg = INIT_VECTOR_QUEUE_MAX_MESSAGES;
    q_init_vector_attr.mq_msgsize = INIT_MSG_SIZE;        
    q_init_vector_attr.mq_curmsgs = 0;                              // initially 0 messages

    int open_flags = O_CREAT | O_RDONLY | O_NONBLOCK;
    mode_t permissions = S_IRUSR | S_IWUSR;             // allow reads and writes into queue

    if ((
        q_init_vector = mq_open(INIT_VECTOR_QUEUE_NAME, open_flags, permissions, 
        &q_init_vector_attr)) == -1)
    {
        res = QUEUE_OPEN_ERROR;
    }
    
    return res;
}



int start_init_vector_thread(struct init_msg* p_init_msg)
{
    int res = REQUEST_THREAD_CREATE_SUCCESS;
    pthread_t th_id;
    if (pthread_create(&th_id, &request_thread_attr, init_vector, p_init_msg) != 0)
    {
        res = REQUEST_THREAD_CREATE_FAIL;
    }
    else // thread created
    {
        // wait until thread copies message
        pthread_mutex_lock(&mutex_msg);
        while (msg_not_copied)
        {
            pthread_cond_wait(&cond_msg, &mutex_msg);
        }
        msg_not_copied = 1; // thread changed it to 0 after copying message, change it to initial state
        pthread_mutex_unlock(&mutex_msg);
    }

    return res;
}



void *init_vector(void* p_init_msg)
{
    struct init_msg init_msg;

    // copy message
    pthread_mutex_lock(&mutex_msg);                                 
    memcpy((char*) &init_msg, (char*) p_init_msg, INIT_MSG_SIZE);   // copy message to local variable
    msg_not_copied = 0;                                             // info for main thread to proceed
    pthread_cond_signal(&cond_msg);               
    pthread_mutex_unlock(&mutex_msg);

    // create vector
    int response = NEW_VECTOR_CREATED;

    printf("name: %s, size: %d, respQueue: %s\n", init_msg.name, init_msg.size, 
        init_msg.resp_queue_name);

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
            perror ("RESPONSE QUEUE could not open response queue");
        }
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

    pthread_attr_destroy(&attr);

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



