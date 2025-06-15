#ifndef TYPES_H
#define TYPES_H

// basic types because i dont wanna type "unsigned char" a million times
// yes i know stdint.h exists, ill use both because why not

#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64; // forgot this one lol
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;

// might need these later idk
typedef unsigned char byte;
typedef unsigned short word;

// boolean because C doesnt have a real one
// yes i know about stdbool.h, shut up
// edit: fine ill include it too
#include <stdbool.h>

#endif