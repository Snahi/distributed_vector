///////////////////////////////////////////////////////////////////////////////////////////////////
// const
///////////////////////////////////////////////////////////////////////////////////////////////////
// init
#define NEW_VECTOR_CREATED 1
#define VECTOR_ALREADY_EXISTS 0
#define VECTOR_CREATION_ERROR -1
// set
#define SET_SUCCESS 0
#define SET_FAIL -1
// get
#define GET_SUCCESS 0
#define GET_FAIL -1
// destroy
#define DESTROY_SUCCESS 1
#define DESTROY_FAIL -1


int init(char* name, int size);
int set(char* name, int pos, int val);
int get(char* name, int pos, int* value);
int destroy(char* vec_name);