#include "array.h"
#include <stdio.h>



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

    printf("SUCCESS: BASIC TEST INIT passed\n");
    return 1;
}



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

    printf("SUCCESS: BASIC TEST SET passed\n");
    return 1;
}



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

    printf("SUCCESS: BASIC TEST GET passed\n");
    return 1;
}



int basic_test()
{
    int init_test = basic_test_init();
    int set_test = basic_test_set();
    int get_test = basic_test_get();

    return init_test && set_test && get_test;
}


int main (int argc, char **argv)
{
    basic_test();
    // for (int i = 0; i < 20; i++)
    // {
    //     char name[7];
    //     sprintf(name, "v%d", i);

    //     int res = init(name, 12);
    //     printf("init res: %d\n", res);

    //     res = set(name, -1, -1);
    //     printf("fail: %d\n", res);

    //     res = set(name, 10, -10);
    //     printf("fail: %d\n", res);

    //     res = set(name, 0, 1);
    //     printf("success: %d\n", res);

    //     res = set(name, 9, 10);
    //     printf("success: %d\n", res);
    // }

    // printf("%d\n", init("a", 3));
    // printf("%d\n", set("a", -2, 2));
    // int val = -1;
    // get("a", 1, &val);
    // printf("%d\n", val);
    // int val = -1;
    // res = get(name, -1, &val);
    // printf("get result (success): %d, expected value 10 : %d\n", res, val);

    // int res = 100;
    // res = destroy("c");
    // printf("destroy result: %d\n", res);

    return 0;
}