#ifndef NODE_TYPES_H
#define NODE_TYPES_H

#include <stdint.h>

// Internal results; these values are not the ERROR packet's wire codes.
typedef enum
{
    NODE_OK = 0,
    NODE_INVALID_ARG,
    NODE_INVALID_FRAME,
    NODE_BUFFER_TOO_SMALL,
    NODE_INIT_FAILED,
    NODE_TIMEOUT,
    NODE_CHECKSUM_ERROR,
    NODE_OUT_OF_RANGE,
    NODE_IO_ERROR,
    NODE_ENCODE_FAILED,
} node_result_t;

typedef struct
{
    int16_t temperature_x10;
    uint16_t humidity_x10;
} node_measurement_t;

#endif
