#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <unistd.h>         // for unblock read function
#include <pthread.h>
#include "vec.h"
#include <dirent.h>



///////////////////////////////////////////////////////////////////////////////////////////////////
// const
///////////////////////////////////////////////////////////////////////////////////////////////////
// user input
#define INITIAL_COMMAND "c"
#define EXIT_COMMAND "q"

// init vector
#define NEW_VECTOR_CREATED 1
#define VECTOR_ALREADY_EXISTS 0
#define VECTOR_CREATION_ERROR -1
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

// set value in vector
#define SET_QUEUE_NAME "/set"
#define SET_QUEUE_MAX_MESSAGES 10
#define SET_SUCCESS 0
#define SET_FAIL -1

struct set_msg {
    char name[MAX_VECTOR_NAME_LEN];
    int pos;
    int value;
    char resp_queue_name[MAX_RESP_QUEUE_NAME_LEN];
};

#define SET_MSG_SIZE sizeof(struct set_msg)

// get value from vector
#define GET_QUEUE_NAME "/get"
#define GET_QUEUE_MAX_MESSAGES 10
#define GET_SUCCESS 0
#define GET_FAIL -1

struct get_msg {
    char name[MAX_VECTOR_NAME_LEN];
    int pos;
    char resp_queue_name[MAX_RESP_QUEUE_NAME_LEN];
};

#define GET_MSG_SIZE sizeof(struct get_msg);

struct get_resp_msg {
    int value;
    int error;
};

// errors
#define QUEUE_OPEN_ERROR 13
#define QUEUE_INIT_SUCCESS 1
#define REQUEST_THREAD_CREATE_SUCCESS 0
#define REQUEST_THREAD_CREATE_FAIL -1

// storage
#define VECTORS_FOLDER "vectors/"
#define VECTOR_FILE_EXTENSION ".txt"
#define TEMP_VECTOR_FILE_EXTENSION ".tmp"

struct vector_mutex {
    char vector_name[MAX_VECTOR_NAME_LEN];
    pthread_mutex_t mutex;
};

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
    creates mutex for every stored vector
*/
int initialize_vector_mutexes();
int destroy_vector_mutexes();
int add_vector_mutex(char* vec_name);
pthread_mutex_t* get_vector_mutex(char* vector_name);
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
int initialize_set_queue();
int initialize_get_queue();
int close_queues();
/*
    creat a vector physically
*/
int create_vector(char* name, int size);
int copy_message(char* p_source, char* p_destination, int size);
void *init_vector(void* p_init_msg);
void* set(void* p_set_msg);
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
mqd_t q_set;
mqd_t q_get;

// storage
struct vector_mutex** vector_mutexes;



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
        // messages
        struct init_msg in_init_msg;
        struct set_msg in_set_msg;

        // listen for requests
        while (strcmp(user_input, EXIT_COMMAND) != 0)
        {
            // read messages in all queues if available
            if (mq_receive(q_init_vector, (char*) &in_init_msg, INIT_MSG_SIZE, NULL) != -1)
            {
                // init queue
                if (start_request_thread(init_vector, &in_init_msg) != REQUEST_THREAD_CREATE_SUCCESS)
                {
                    perror("REQUEST THREAD could not create thread for init vector request");
                }
            }

            if (mq_receive(q_set, (char*) &in_set_msg, SET_MSG_SIZE, NULL) != -1)
            {
                if (start_request_thread(set, &in_set_msg) != REQUEST_THREAD_CREATE_SUCCESS)
                {
                    perror("REQUEST THREAD could not create thread for set value request");
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

    if (!destroy_vector_mutexes())
        perror("CLEAN UP couldnt destroy vector files mutexes");

    close_queues();
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

    if (!initialize_vector_mutexes())
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

    if (initialize_set_queue() != QUEUE_INIT_SUCCESS)
    {
        perror("INIT ERROR: could not open set queue");
        return 0;
    }

    if (initialize_get_queue() != QUEUE_INIT_SUCCESS)
    {
        perror("INIT ERROR: could not open get queue");
        return 0;
    }

    return 1;
}



int add_vector_mutex(char* vec_name)
{
    struct vector_mutex* p_vec_mut = (struct vector_mutex*) malloc(sizeof(struct vector_mutex));

    strcpy(p_vec_mut->vector_name, vec_name);
    vector_add(&vector_mutexes, p_vec_mut);
    
    if (pthread_mutex_init(&p_vec_mut->mutex, NULL) != 0)
        return 0;

    return 1;
}



int initialize_vector_mutexes()
{
    int res = 1;

    DIR* vec_dir;
    struct dirent* vec_dir_ent;

    if ((vec_dir = opendir(VECTORS_FOLDER)) != NULL)
    {
        vector_mutexes = vector_create();

        int extension_len = strlen(VECTOR_FILE_EXTENSION);
        char extension[extension_len + 1];
        extension[extension_len] = '\0';
        char f_name[MAX_VECTOR_NAME_LEN + extension_len + 1];
        int f_name_len = 0;
        char f_name_no_extension[MAX_VECTOR_NAME_LEN];

        while ((vec_dir_ent = readdir(vec_dir)) != NULL)
        {
            strcpy(f_name, vec_dir_ent->d_name);
            f_name_len = strlen(f_name);
            if (f_name_len > extension_len) // ignore non vector files (too short name)
            {
                strncpy(extension, f_name + f_name_len - extension_len, extension_len);
                if (strcmp(extension, VECTOR_FILE_EXTENSION) == 0)  // ignore files with wrong extension
                {
                    strncpy(f_name_no_extension, f_name, f_name_len - extension_len);
                    add_vector_mutex(f_name_no_extension);
                }
            }
        }

        if (closedir(vec_dir) != 0)
            res = 0;
    }
    else
        res = 0;

    return res;
}



int destroy_vector_mutexes()
{
    int size = vector_size(vector_mutexes);
    for (int i = 0; i < size; i++)
    {
        if (pthread_mutex_destroy(&vector_mutexes[i]->mutex) != 0)
            perror("DESTROY VECTOR MUTEXES cannot destroy mutex");

        free(vector_mutexes[i]);
    }
}



pthread_mutex_t* get_vector_mutex(char* vector_name)
{
    int size = vector_size(vector_mutexes);
    for (int i = 0; i < size; i++)
    {
        if (strcmp(vector_mutexes[i]->vector_name, vector_name) == 0)
        {
            return &vector_mutexes[i]->mutex;
        }
    }

    return NULL;
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
    pthread_mutex_t* p_vec_file_mutex = NULL;

    if (add_vector_mutex(name)) // create mutex for new vector
    {
        p_vec_file_mutex = get_vector_mutex(name); // acquire newly created mutex
        
        if (p_vec_file_mutex != NULL)
        {
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
                        perror("CREATE ARRAY FILE could not initialize file");
                    }
                    
                    if (fclose(fp) != 0)
                    {
                        res = 0;
                        perror("CREATE ARRAY FILE could not close file descriptor");
                    }

                    if (pthread_mutex_unlock(p_vec_file_mutex) != 0)
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
        else // NULL vector mutex
        {
            res = 0;
            printf("CREATE ARRAY FILE null vector mutex\n");
        }
    }
    else // couldn't create vector mutex
    {
        res = 0;
        perror("CREATE ARRAY FILE could not create vector mutex");
    }

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
    
    pthread_mutex_t* mutex_vec_file;
    if ((mutex_vec_file = get_vector_mutex(vec_name)) != NULL) // obtain mutex for the vector file
    {
        if (pthread_mutex_lock(mutex_vec_file) == 0)    // lock mutex for the vector file
        {
            FILE* p_old, *p_new;

            if ((p_old = fopen(full_vector_file_name, "r")) != NULL)    // open current vector file
            {
                if ((p_new = fopen(temp_file_name, "w")) != NULL)   // open temporal vector file
                {
                    char line[20];  // buffer for reading old file
                    int line_idx = -1; // -1 because first line is size of vector
                    while(fgets(line, 20, p_old) != NULL)
                    {
                        if (line_idx != pos)    // not specified position
                        {
                            if (fputs(line, p_new) < 0) // copy from old to new file
                            {
                                res = SET_FAIL;
                                break;
                            }
                        }
                        else // reached required position
                        {
                            if (fprintf(p_new, "%d\n", val) < 0)    // print new value
                            {
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
                    fclose(p_old);  // I don't check for success, function failed anyway
                }  
            }
            else // can't open old vector file
                res = SET_FAIL;   

            if (pthread_mutex_unlock(mutex_vec_file) != 0)
                res = SET_FAIL;      
        }
        else // can't lock mutex
            res = SET_FAIL;
    }
    else // can't obtain mutex
        res = SET_FAIL;

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
        perror("SET couldn't copy_message");
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
        res = QUEUE_OPEN_ERROR;
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



