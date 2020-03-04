#include "array.h"
#include <stdio.h>
#include <pthread.h>



// init test //////////////////////////////////////////////////////////////////////////////////////



int basic_test_init()
{
    // proper data
    char proper_name[] = "mypropervector";
    int res = init(proper_name, 10);
    if (res != 1)
    {
        printf("FAIL: BASIC TEST INIT proper vector couldn't be created\n");
        return 0;
    }

    // the same vector created again
    res = init(proper_name, 10);
    if (res != 0)
    {
        printf("FAIL: BASIC TEST INIT the same vector created again didn't return 0\n");
        return 0;
    }

    // the same vector with different size
    res = init(proper_name, 9);
    if (res != -1)
    {
        printf("FAIL: BASIC TEST INIT vector with the same name but different length\n");
        return 0;
    }

    // incorrect name
    char incorrect_name[] = "**$#@@*";
    res = init(incorrect_name, 10);
    if (res != -1)
    {
        printf("FAIL: BASIC TEST INIT vector with wrong name\n");
    }

    // negative size
    char correct_name_2[] = "negativesize";
    res = init(correct_name_2, -3);
    if (res != -1)
    {
        printf("FAIL: BASIC TEST INIT vector with size -3\n");
        return 0;
    }

    res = init(correct_name_2, -1);
    if (res != -1)
    {
        printf("FAIL: BASIC TEST INIT vector with size -1\n");
        return 0;
    }

    res = init(correct_name_2, 0);
    if (res != -1)
    {
        printf("FAIL: BASIC TEST INIT vector with size 0\n");
        return 0;
    }

    // clean up
    if (remove("vectors/mypropervector.txt") != 0)
    {
        printf("FAIL: BASIC TEST INIT clean up error\n");
        return 0;
    }

    printf("SUCCESS: BASIC TEST INIT passed\n");
    return 1;
}



// set test ///////////////////////////////////////////////////////////////////////////////////////



int basic_test_set()
{
    char vec_name[] = "setvec";
    if (init(vec_name, 10) == -1)
    {
        printf("FAIL: BASIC TEST SET could not initialize vector\n");
        return 0;
    }

    // proper set
    int res = set(vec_name, 0, 1);
    if (res != 0)
    {
        printf("FAIL: BASIC TEST SET proper set\n");
        return 0;
    }

    res = set(vec_name, 1, 2);
    if (res != 0)
    {
        printf("FAIL: BASIC TEST SET proper set\n");
        return 0;
    }

    res = set(vec_name, 2, 3);
    if (res != 0)
    {
        printf("FAIL: BASIC TEST SET proper set\n");
        return 0;
    }

    res = set(vec_name, 9, 10);
    if (res != 0)
    {
        printf("FAIL: BASIC TEST SET proper set\n");
        return 0;
    }

    // negative position
    res = set(vec_name, -1, -1);
    if (res != -1)
    {
        printf("FAIL: BASIC TEST SET -1 set at postion -1\n");
        return 0;
    }

    res = set(vec_name, -2, -2);
    if (res != -1)
    {
        printf("FAIL: BASIC TEST SET -2 set at postion -2\n");
        return 0;
    }

    // too big position
    res = set(vec_name, 10, 11);
    if (res != -1)
    {
        printf("FAIL: BASIC TEST SET value set at position 10 while array size is 10\n");
        return 0;
    }

    res = set(vec_name, 11, 12);
    if (res != -1)
    {
        printf("FAIL: BASIC TEST SET value set at position 11 while array size is 11\n");
        return 0;
    }

    // clean up
    if (remove("vectors/setvec.txt") != 0)
    {
        printf("FAIL: BASIC TEST INIT clean up error\n");
        return 0;
    }

    printf("SUCCESS: BASIC TEST SET passed\n");
    return 1;
}



// get test ///////////////////////////////////////////////////////////////////////////////////////



int basic_test_get()
{
    char vec_name[] = "getvec";
    if (init(vec_name, 20) != 1)
    {
        printf("FAIL: BASIC TEST GET could not initialize vector\n");
        return 0;
    }

    for (int i = 0; i < 20; i++)
    {
        if (set(vec_name, i, i + 1) == -1)
        {
            printf("FAIL: BASIC TEST GET could not set values\n");
            return 0;
        }
    }

    // proper get
    int value = -1;
    for (int i = 0; i < 20; i ++)
    {
        if (get(vec_name, i, &value) != 0)
        {
            printf("FAIL: BASIC TEST GET could not get value");
            return 0;
        }

        if (value != i + 1)
        {
            printf("FAIL: BASIC TEST GET wrong value returned\n");
            return 0;
        }
    }

    // negative position
    int res = get(vec_name, -3, &value);
    if (res != -1)
    {
        printf("FAIL: BASIC TEST GET negative position -3\n");
        return 0;
    }

    res = get(vec_name, -1, &value);
    if (res != -1)
    {
        printf("FAIL: BASIC TEST GET negative position -1\n");
        return 0;
    }

    // too big position
    res = get(vec_name, 20, &value);
    if (res != -1)
    {
        printf("FAIL: BASIC TEST GET too big position 20\n");
        return 0;
    }

    res = get(vec_name, 21, &value);
    if (res != -1)
    {
        printf("FAIL: BASIC TEST GET too big position 21\n");
        return 0;
    }

    // non existing vector
    res = get("asfsadfasfsadsasfa", 2, &value);
    if (res != -1)
    {
        printf("FAIL: BASIC TEST GET non existing vector\n");
        return 0;
    }

    // clean up
    if (remove("vectors/getvec.txt") != 0)
    {
        printf("FAIL: BASIC TEST INIT clean up error\n");
        return 0;
    }

    printf("SUCCESS: BASIC TEST GET passed\n");
    return 1;
}



// destroy test ///////////////////////////////////////////////////////////////////////////////////



int basic_test_destroy()
{
    char name[] = "todestroy";
    if (init(name, 10) != 1)
    {
        printf("FAIL: BASIC TEST DESTROY could not initialize vector\n");
        return 0;
    }

    // proper destroy
    if (destroy(name) != 1)
    {
        printf("FAIL: BASIC TEST DESTROY could not destory vector\n");
        return 0;
    }

    // destroying nonexisting vector
    if (destroy(name) != -1)
    {
        printf("FAIL: BASIC TEST DESTROY destroyed nonexisting vector\n");
        return 0;
    }

    printf("SUCCESS: BASIC TEST DESTROY passed\n");
    return 1;
}



// all basic tests ////////////////////////////////////////////////////////////////////////////////



int basic_test()
{
    int init_test = basic_test_init();
    int set_test = basic_test_set();
    int get_test = basic_test_get();
    int destroy_test = basic_test_destroy();

    return init_test && set_test && get_test && destroy_test;
}



// multithreaded test /////////////////////////////////////////////////////////////////////////////



void* set_test_thread(void* p_args)
{
    char* vec_name = (char*) p_args;
    
    for (int i = 0; i < 10000; i++)
    {
        if (set(vec_name, i, i) != 0)
        {
            printf("%d\n", i);
            printf("FAIL: TEST THREAD could not set value\n");
            pthread_exit(NULL);
        }
    }

    pthread_exit(NULL);
}



void* get_test_thread(void* p_args)
{
    char* vec_name = (char*) p_args;

    int val = -1;
    for (int i = 0; i < 10000; i++)
    {
        if (get(vec_name, i, &val) != 0)
        {
            printf("FAIL: GET TEST THREAD could not get a value\n");
            pthread_exit(NULL);
        }

        if (val != i)
        {
            printf("FAIL: GET TEST THREAD wrong value\n");
            pthread_exit(NULL);
        }
    }

    pthread_exit(NULL);
}



int multithreaded_test()
{
    char vec_name[] = "multithreaded";
    if (init(vec_name, 10000) != 1)
    {
        printf("FAIL: MULTITHREADED TEST could not initialize the vector\n");
        return 0;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    pthread_t t_set_1, t_set_2;

    if (pthread_create(&t_set_1, &attr, &set_test_thread, (void*) vec_name) != 0 ||
        pthread_create(&t_set_2, &attr, &set_test_thread, (void*) vec_name) != 0)
    {
        printf("FAIL: MULTITHREADED TEST could not create set threads\n");
        return 0;
    }
    
    if (pthread_join(t_set_1, NULL) != 0 ||
        pthread_join(t_set_2, NULL) != 0)
    {
        printf("FAIL: MULTITHREADED TEST could not join set threads\n");
        return 0;
    } 

    // check whether set values are ok
    pthread_t t_get_1, t_get_2;

    if (pthread_create(&t_get_1, &attr, get_test_thread, (void*) vec_name) != 0 ||
        pthread_create(&t_get_2, &attr, get_test_thread, (void*) vec_name) != 0)
    {
        printf("FAIL: MULTITHREADED TEST could not create get threads\n");
        return 0;
    }

    if (pthread_join(t_get_1, NULL) != 0 ||
        pthread_join(t_get_2, NULL) != 0)
    {
        printf("FAIL: MULTITHREADED TEST could not join get threads\n");
        return 0;
    } 

    // destroy
    if (destroy(vec_name) != 1)
    {
        printf("FAIL: MULTITHREADED TEST could not destroy\n");
        return 0;
    }

    if (pthread_attr_destroy(&attr) != 0)
    {
        printf("FAIL: MULTITHREADED TEST could not destroy attributes\n");
        return 0;
    }

    printf("SUCCESS: MULTITHREADED TEST passed\n");
    return 1;
}



// all tests //////////////////////////////////////////////////////////////////////////////////////



int all_tests()
{
    int res = 1;
    int basic_test_res = basic_test();
    int multi_test_res = multithreaded_test();

    if (basic_test_res && multi_test_res)
        printf("GLOBAL SUCCESS: ALL TESTS PASSED\n");
    else
    {
        printf("GLOBAL FAILURE: TESTS FAILED\n");
        res = 0;
    }

    return res;
}





int main (int argc, char **argv)
{
    // required minimum
    init("vector1", 100);
    init("vector2", 200);
    set("vector1", 0, 40);
    set("vector1", 120, 30);
    init("vector1", 200);
    destroy("vector1");
    destroy("vector");

    // additional tests
    all_tests();

    return 0;
}