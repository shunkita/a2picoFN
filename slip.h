/*

MIT License

Copyright (c) 2026 Oliver Schmidt (https://a2retro.de/)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#ifndef _SLIP_H
#define _SLIP_H

#include <stdint.h>
#include <stdbool.h>

#define SP_CMD_STATUS   0x00
#define SP_CMD_READBLK  0x01
#define SP_CMD_WRITEBLK 0x02
#define SP_CMD_CONTROL  0x04
#define SP_CMD_INIT     0x05
#define SP_CMD_WRITE    0x09

#define SP_REQ_SIZE     10
#define BLK_SIZE        512

#define SP_I_CMD        0
#define SP_I_PARAM_CNT  1
#define SP_I_UNIT       2
#define SP_I_STS_CODE   5
#define SP_I_BLK_BUFF   3
#define SP_I_BLK_BLOCK  5
#define SP_I_BLK_BUFFER 10
#define SP_I_CTRL_CODE  5
#define SP_I_CTRL_CNT   10
#define SP_I_WR_CNT     5

#define SP_O_RETVAL     0
#define SP_O_STS_CNT    1
#define SP_O_STS_LIST   3
#define SP_O_BLK_BUFFER 1

#define SP_STATUS_STS   0x00
#define SP_STATUS_DIB   0x03

#define SP_SUCCESS      0x00
#define SP_BADCMD       0x01
#define SP_BUSERR       0x06
#define SP_BADCTL       0x21
#define SP_NODRIVE      0x28

void slip_reset(void);

bool slip_connected(void);

uint8_t slip_units(void);

void slip_command(uint8_t control, uint8_t *buffer);

void slip_task(void);

#endif
