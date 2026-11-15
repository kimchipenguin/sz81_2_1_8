/*
 * sz81 - workarounds for old AES versions (TOS 1.x - 4.x)
 */

#ifndef AESFIX_H
#define AESFIX_H

#include <gem.h>

/* let all USERDEF objects of a tree draw on our own stack */
void aesfix_userdefs(OBJECT *tree);

#endif
