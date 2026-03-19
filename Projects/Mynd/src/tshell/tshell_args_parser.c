#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>

#include "tshell.h"
#include "tshell_args_parser.h"

// Generic function
static int tshell_parse_args_long(size_t argc, char** argv, long *result)
{
    (void)(argc);
    const char *nptr = argv[1];
    char *endptr = NULL;

    // if (min > max)
    // {
    //     printf("Input parse: invalid MIN/MAX values\r\n");
    //     return -1;
    // }

    if (/*argc == 0 ||*/ argv == NULL || argv[1] == NULL)
        return -2;

    *result = strtol(nptr, &endptr, 0);
    if (nptr == endptr)
    {
        printf("Cannot parse arguments\r\n");
        // log_errt("Cannot parse arguments\r\n");
        return -3;
    }

    return 0;
}

static int tshell_parse_args_ulong(size_t argc, char** argv, unsigned long *result)
{
    (void)(argc);
    const char *nptr = argv[1];
    char *endptr = NULL;

    // if (min > max)
    // {
    //     printf("Input parse: invalid MIN/MAX values\r\n");
    //     return -1;
    // }

    if (/*argc == 0 ||*/ argv == NULL || argv[1] == NULL)
        return -2;

    *result = strtoul(nptr, &endptr, 0);
    if (nptr == endptr)
    {
        printf("Cannot parse arguments\r\n");
        // log_errt("Cannot parse arguments\r\n");
        return -3;
    }

    return 0;
}

int tshell_parse_args_uint8(size_t argc, char** argv, uint8_t min, uint8_t max, uint8_t *result)
{
    unsigned long parsed_long_value;

    if (tshell_parse_args_ulong(argc, argv, &parsed_long_value) < 0)
        return -1;

    if (parsed_long_value > UINT8_MAX)
    {
        printf("Cannot parse argument - out of range\r\n");
        return -4;
    }

    *result = (uint8_t)parsed_long_value;

    if (*result < min || *result > max)
    {
        printf("Cannot set %u value, must be in range [%u, %u]\r\n", *result, min, max);
        return -5;
    }

    return 0;
}

int tshell_parse_args_int8(size_t argc, char** argv, int8_t min, int8_t max, int8_t *result)
{
    long parsed_long_value;

    if (tshell_parse_args_long(argc, argv, &parsed_long_value) < 0)
        return -1;

    if (parsed_long_value < INT8_MIN || parsed_long_value > INT8_MAX)
    {
        printf("Cannot parse argument - out of range\r\n");
        return -4;
    }

    *result = (int8_t)parsed_long_value;

    if (*result < min || *result > max)
    {
        printf("Cannot set %d value, must be in range [%d, %d]\r\n", *result, min, max);
        return -5;
    }

    return 0;
}

int tshell_parse_args_uint32(size_t argc, char** argv, uint32_t min, uint32_t max, uint32_t *result)
{
    unsigned long parsed_long_value;

    if (tshell_parse_args_ulong(argc, argv, &parsed_long_value) < 0)
        return -1;

    if (parsed_long_value > UINT32_MAX)
    {
        printf("Cannot parse argument - out of range\r\n");
        return -4;
    }

    *result = (uint32_t)parsed_long_value;

    if (*result < min || *result > max)
    {
        printf("Cannot set %lu value, must be in range [%lu, %lu]\r\n", *result, min, max);
        return -5;
    }

    return 0;
}

int tshell_parse_args_int32(size_t argc, char** argv, int32_t min, int32_t max, int32_t *result)
{
    long parsed_long_value;

    if (tshell_parse_args_long(argc, argv, &parsed_long_value) < 0)
        return -1;

    if (parsed_long_value < INT32_MIN || parsed_long_value > INT32_MAX)
    {
        printf("Cannot parse argument - out of range\r\n");
        return -4;
    }

    *result = (int32_t)parsed_long_value;

    if (*result < min || *result > max)
    {
        printf("Cannot set %d value, must be in range [%d, %d]\r\n", *result, min, max);
        return -5;
    }

    return 0;
}
