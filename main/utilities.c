/*
 * Copyright (C) u-blox Melbourn Ltd
 * u-blox Melbourn Ltd, Melbourn, UK
 *
 * All rights reserved.
 *
 * This source file is the sole property of u-blox Melbourn Ltd.
 * Reproduction or utilisation of this source in whole or part is
 * forbidden without the written consent of u-blox Melbourn Ltd.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "utilities.h"

// ----------------------------------------------------------------
// COMPILE-TIME CONSTANTS
// ----------------------------------------------------------------

// ----------------------------------------------------------------
// TYPES
// ----------------------------------------------------------------

// ----------------------------------------------------------------
// PRIVATE VARIABLES
// ----------------------------------------------------------------

static const char hexTable[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

// ----------------------------------------------------------------
// PUBLIC VARIABLES
// ----------------------------------------------------------------

// ----------------------------------------------------------------
// STATIC FUNCTIONS
// ----------------------------------------------------------------

// ----------------------------------------------------------------
// PUBLIC FUNCTIONS
// ----------------------------------------------------------------

// Convert a hex string of a given length into a sequence of bytes, returning the
// number of bytes written.
int utilitiesHexStringToBytes(const char *pInBuf, int lenInBuf, char *pOutBuf, int lenOutBuf)
{
    int y = 0;
    int z;
    int a = 0;

    for (int x = 0; (x < lenInBuf) && (y < lenOutBuf); x++) {
        z = *(pInBuf + x);
        if ((z >= '0') && (z <= '9')) {
            z = z - '0';
        } else {
            z &= ~0x20;
            if ((z >= 'A') && (z <= 'F')) {
                z = z - 'A' + 10;
            } else {
                z = -1;
            }
        }

        if (z >= 0) {
            if (a % 2 == 0) {
                *(pOutBuf + y) = (z << 4) & 0xF0;
            } else {
                *(pOutBuf + y) += z;
                y++;
            }
            a++;
        }
    }

    return y;
}

// Convert a sequence of bytes into a hex string, returning the number
// of characters written. The hex string is NOT null terminated.
int utilitiesBytesToHexString(const char *pInBuf, int lenInBuf, char *pOutBuf, int lenOutBuf)
{
    int y = 0;

    for (int x = 0; (x < lenInBuf) && (y < lenOutBuf); x++) {
        pOutBuf[y] = hexTable[(pInBuf[x] >> 4) & 0x0f]; // upper nibble
        y++;
        if (y < lenOutBuf) {
            pOutBuf[y] = hexTable[pInBuf[x] & 0x0f]; // lower nibble
            y++;
        }
    }

    return y;
}

// A simple implementation of atoi() for positive numbers only.
int asciiToInt(const char *pBuf)
{
    unsigned int answer = 0;

     for (int x = 0; *pBuf != 0; x++) {
         answer = answer * 10 + *pBuf - '0';
         pBuf++;
     }

     return (int) answer;
}

// End Of File
