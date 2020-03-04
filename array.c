#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <regex.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "array.h"



///////////////////////////////////////////////////////////////////////////////////////////////////
// const
///////////////////////////////////////////////////////////////////////////////////////////////////
#define NAME_REGEX "^[a-zA-Z0-9]+$"

// init ///////////////////////////////////////////////////////////////////////////////////////////
#define INIT_VECTOR_QUEUE_NAME "/init"
#define INIT_RESP_QUEUE_PREFIX "initvec"
#define INIT_VECTOR_QUEUE_MAX_MESSAGES 10
#define MAX_VECTOR_NAME_LEN 40
#define MAX_RESP_QUEUE_NAME_LEN 64

struct init_msg {
    char name[MAX_VECTOR_NAME_LEN];
    int size;
    char resp_queue_name[MAX_RESP_QUEUE_NAME_LEN];
};

#define INIT_MSG_SIZE sizeof(struct init_msg)

// set ////////////////////////////////////////////////////////////////////////////////////////////
#define SET_QUEUE_NAME "/set"
#define SET_RESP_QUEUE_PREFIX "setval"

struct set_msg {
    char name[MAX_VECTOR_NAME_LEN];
    int pos;
    int value;
    char resp_queue_name[MAX_RESP_QUEUE_NAME_LEN];
};

#define SET_MSG_SIZE sizeof(struct set_msg)

// get ////////////////////////////////////////////////////////////////////////////////////////////
#define GET_QUEUE_NAME "/get"
#define GET_RESP_QUEUE_PREFIX "getval"

struct get_msg {
    char name[MAX_VECTOR_NAME_LEN];
    int pos;
    char resp_queue_name[MAX_RESP_QUEUE_NAME_LEN];
};

#define GET_MSG_SIZE sizeof(struct get_msg)

struct get_resp_msg {
    int value;
    int error;
};

#define GET_RESP_MSG_SIZE sizeof(struct get_resp_msg)

// destroy ////////////////////////////////////////////////////////////////////////////////////////
#define DESTROY_QUEUE_NAME "/destroy"
#define DESTROY_RESP_QUEUE_PREFIX "destr"

struct destroy_msg {
    char name[MAX_VECTOR_NAME_LEN];
    char resp_queue_name[MAX_RESP_QUEUE_NAME_LEN];
};

#define DESTROY_MSG_SIZE sizeof(struct destroy_msg)



///////////////////////////////////////////////////////////////////////////////////////////////////
// function declarations
///////////////////////////////////////////////////////////////////////////////////////////////////



int is_init_data_valid(char* name, int size);
int is_name_valid(char* name);
int open_server_init_queue(mqd_t* p_queue);
/*
    creates and opens a queue with unique name for getting a response from the server
*/
int open_resp_queue(char* prefix, char* que_name, mqd_t* p_queue, size_t msg_size);
int create_vector_on_server(char* name, int size, char* resp_que_name, mqd_t* p_q_server, 
    mqd_t* p_q_resp);



///////////////////////////////////////////////////////////////////////////////////////////////////
// init
///////////////////////////////////////////////////////////////////////////////////////////////////



int init(char* name, int size)
{
    int result = NEW_VECTOR_CREATED;

    if (is_init_data_valid(name, size))
    {
        // open queue to send init vector message to server
        mqd_t q_server_init;

        if ((q_server_init = mq_open(INIT_VECTOR_QUEUE_NAME, O_WRONLY)) == -1)
        {
            result = VECTOR_CREATION_ERROR;
        }
        else
        {
            // queue for response from server
            mqd_t q_resp;
            char resp_que_name[MAX_RESP_QUEUE_NAME_LEN];
            if (open_resp_queue(INIT_RESP_QUEUE_PREFIX, resp_que_name, &q_resp, sizeof(int)) == 1)
            {
                result = create_vector_on_server(name, size, resp_que_name, &q_server_init, &q_resp);

                // close and unlink response queue
                if (mq_close (q_resp) == -1)
                    result = VECTOR_CREATION_ERROR;

                if (mq_unlink(resp_que_name) == -1)
                    result = VECTOR_CREATION_ERROR;
            }
            else // couldn't open response queue
                result = VECTOR_CREATION_ERROR;

            if (mq_close (q_server_init) == -1) 
                result = VECTOR_CREATION_ERROR;
        }
    }
    else 
        result = VECTOR_CREATION_ERROR;
    
    return result;
}



int is_init_data_valid(char* name, int size)
{
    int is_size_val = size > 0;
    int is_name_val = is_name_valid(name);

    return is_size_val && is_name_val;
}



int is_name_valid(char* name)
{
    int res = 1;
    size_t len = strlen(name);

    if (len >= 1 && len <= MAX_VECTOR_NAME_LEN)
    {
        regex_t regex;
        if (regcomp(&regex, NAME_REGEX, REG_EXTENDED | REG_NOSUB) == 0)
        {
            if (regexec(&regex, name, 0, NULL, 0) == 0)
            {
                res = 1;
            }
            else
            {
                res = 0;
            }
            regfree(&regex);
        }
        else
            res = 0;
    }
    else
        res = 0;
    
    return res;
}



int create_vector_on_server(char* name, int size, char* resp_que_name, mqd_t* p_q_server, 
    mqd_t* p_q_resp)
{
    int result = NEW_VECTOR_CREATED;

    // create message
    struct init_msg msg;
    msg.size = size;
    strcpy(msg.name, name);
    strcpy(msg.resp_queue_name, resp_que_name);

    // send message
    if (mq_send(*p_q_server, (char*) &msg, INIT_MSG_SIZE, 0) == -1)
        result = VECTOR_CREATION_ERROR;
    else // message send successfully
    {
        // wait for response
        if (mq_receive(*p_q_resp, (char*) &result, sizeof(int), NULL) == -1)
            result = VECTOR_CREATION_ERROR;
    }

    return result;
}



///////////////////////////////////////////////////////////////////////////////////////////////////
// set
///////////////////////////////////////////////////////////////////////////////////////////////////



int set_on_server(char* name, int pos, int val, char* resp_que_name, mqd_t* p_q_server, 
    mqd_t* p_q_resp)
{
    int result = SET_SUCCESS;

    // create message
    struct set_msg msg;
    msg.pos = pos;
    msg.value = val;
    strcpy(msg.name, name);
    strcpy(msg.resp_queue_name, resp_que_name);

    // send message
    if (mq_send(*p_q_server, (char*) &msg, SET_MSG_SIZE, 0) == -1)
        result = SET_FAIL;
    else // message send successfully
    {
        // wait for response
        if (mq_receive(*p_q_resp, (char*) &result, sizeof(int), NULL) == -1)
            result = SET_FAIL;
    }

    return result;
}



int set(char* name, int pos, int val)
{
    int result = SET_SUCCESS;
    // open queue to send set message to server
    mqd_t q_server_set;

    if ((q_server_set = mq_open(SET_QUEUE_NAME, O_WRONLY)) == -1)
        result = SET_FAIL;
    else
    {
        // queue for response from server
        mqd_t q_resp;
        char resp_que_name[MAX_RESP_QUEUE_NAME_LEN];
        if (open_resp_queue(SET_RESP_QUEUE_PREFIX, resp_que_name, &q_resp, sizeof(int)) == 1)
        {
            result = set_on_server(name, pos, val, resp_que_name, &q_server_set, &q_resp);

            // close and delete response queue
            if (mq_close (q_resp) == -1)
                result = SET_FAIL;

            if (mq_unlink(resp_que_name) == -1)
                result = SET_FAIL;
        }
        else // couldn't open response queue
            result = SET_FAIL;

        if (mq_close(q_server_set) == -1) 
            result = SET_FAIL;
    }

    return result;
}



///////////////////////////////////////////////////////////////////////////////////////////////////
// get
///////////////////////////////////////////////////////////////////////////////////////////////////



int get_from_server(char* name, int pos, int* p_val, char* resp_que_name, mqd_t* p_q_server, 
    mqd_t* p_q_resp)
{
    int result = GET_SUCCESS;

    // create message
    struct get_msg msg;
    msg.pos = pos;
    strcpy(msg.name, name);
    strcpy(msg.resp_queue_name, resp_que_name);

    // send message
    if (mq_send(*p_q_server, (char*) &msg, GET_MSG_SIZE, 0) == -1)
        result = GET_FAIL;
    else // message send successfully
    {
        struct get_resp_msg response;

        // wait for response
        if (mq_receive(*p_q_resp, (char*) &response, GET_RESP_MSG_SIZE, NULL) == -1)
            result = SET_FAIL;
        else
        {
            result = response.error;
            *p_val = response.value;
        }
        
    }

    return result;
}


int get(char* name, int pos, int* value)
{
    int result = GET_SUCCESS;
    // open queue to send get message to server
    mqd_t q_server_get;

    if ((q_server_get = mq_open(GET_QUEUE_NAME, O_WRONLY)) == -1)
        result = GET_FAIL;
    else
    {
        // queue for response from server
        mqd_t q_resp;
        char resp_que_name[MAX_RESP_QUEUE_NAME_LEN];
        if (open_resp_queue(GET_RESP_QUEUE_PREFIX, resp_que_name, &q_resp, GET_RESP_MSG_SIZE) == 1)
        {
            result = get_from_server(name, pos, value, resp_que_name, &q_server_get, &q_resp);

            // close and delete response queue
            if (mq_close (q_resp) == -1)
                result = SET_FAIL;

            if (mq_unlink(resp_que_name) == -1)
                result = SET_FAIL;
        }
        else // couldn't open response queue
            result = SET_FAIL;

        if (mq_close(q_server_get) == -1) 
            result = SET_FAIL;
    }

    return result;
}



///////////////////////////////////////////////////////////////////////////////////////////////////
// destroy
///////////////////////////////////////////////////////////////////////////////////////////////////



int destroy_on_server(char* name, char* resp_que_name, mqd_t* p_q_server, mqd_t* p_q_resp)
{
    int result = DESTROY_SUCCESS;

    // create message
    struct destroy_msg msg;
    strcpy(msg.name, name);
    strcpy(msg.resp_queue_name, resp_que_name);
    
    // send message
    if (mq_send(*p_q_server, (char*) &msg, DESTROY_MSG_SIZE, 0) == -1)
        result = GET_FAIL;
    else // message send successfully
    {
        // wait for response
        if (mq_receive(*p_q_resp, (char*) &result, sizeof(int), NULL) == -1)
            result = SET_FAIL;
    }

    return result;
}



int destroy(char* vec_name)
{
    int result = DESTROY_SUCCESS;
    // open queue to send destroy message to server
    mqd_t q_server_destroy;

    if ((q_server_destroy = mq_open(DESTROY_QUEUE_NAME, O_WRONLY)) == -1)
        result = DESTROY_FAIL;
    else
    {
        // queue for response from server
        mqd_t q_resp;
        char resp_que_name[MAX_RESP_QUEUE_NAME_LEN];
        if (open_resp_queue(DESTROY_RESP_QUEUE_PREFIX, resp_que_name, &q_resp, sizeof(int)) == 1)
        {
            result = destroy_on_server(vec_name, resp_que_name, &q_server_destroy, &q_resp);

            // close and delete response queue
            if (mq_close(q_resp) == -1)
                result = SET_FAIL;

            if (mq_unlink(resp_que_name) == -1)
                result = SET_FAIL;
        }
        else // couldn't open response queue
            result = SET_FAIL;

        if (mq_close(q_server_destroy) == -1) 
            result = SET_FAIL;
    }

    return result;
}



///////////////////////////////////////////////////////////////////////////////////////////////////
// general
///////////////////////////////////////////////////////////////////////////////////////////////////



int open_resp_queue(char* prefix, char* que_name, mqd_t* p_queue, size_t msg_size)
{
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 1;
    attr.mq_msgsize = msg_size;
    attr.mq_curmsgs = 0;

    char local_que_name[MAX_RESP_QUEUE_NAME_LEN];

    // make queue name with PID, in single thread client it will be unique
    snprintf(local_que_name, MAX_RESP_QUEUE_NAME_LEN, "/%s%d", prefix, getpid());
    // flags which assure that queue with such name doesn't exist
    int flags = O_CREAT | O_EXCL | O_RDONLY;
    // will be used to generate unique name in case it's needed
    char random_str[] = "1";
    // if name is not unique, add random number to it. If there is no more space
    // start from the beginning
    while((*p_queue = mq_open(local_que_name, flags, S_IRUSR | S_IWUSR, &attr)) == -1)
    {
        if (errno == EEXIST)
        {
            // reached maximum name len, reset the name
            if (strlen(local_que_name) == MAX_RESP_QUEUE_NAME_LEN - 1)
            {
                snprintf(local_que_name, MAX_RESP_QUEUE_NAME_LEN, "/%s%d", prefix, getpid());
            }
            else
            {
                // generate random number and convert it to string
                snprintf(random_str, 2, "%d", rand() % 10);
                // append generated number to the name
                strcat(local_que_name, random_str);
            }
        }
        else // error occurred (but not because of non unique name)
            return 0;
    }

    strcpy(que_name, local_que_name);
    
    return 1;
}