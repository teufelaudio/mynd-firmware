#pragma once

#if defined(__cplusplus)
extern "C"
{
#endif /* __cplusplus */

int tshell_parse_args_uint8(size_t argc, char** argv, uint8_t min, uint8_t max, uint8_t *result);
int tshell_parse_args_int8(size_t argc, char** argv, int8_t min, int8_t max, int8_t *result);
int tshell_parse_args_uint32(size_t argc, char** argv, uint32_t min, uint32_t max, uint32_t *result);
int tshell_parse_args_int32(size_t argc, char** argv, int32_t min, int32_t max, int32_t *result);

#if defined(__cplusplus)
}
#endif /* __cplusplus */
