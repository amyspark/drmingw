/**************************************************************************
 *
 * Copyright 2016 Jose Fonseca
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OF OR CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 **************************************************************************/

#include <windows.h>

#include "macros.h"

int
main(int argc, char *argv[])
{
    BOOL bRet;
    bRet = GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);  Sleep(1000);  LINE_BARRIER
    return bRet ? 0 : 125;
}

// CHECK_STDERR: /^catchsegv: warning: caught Ctrl-C event/
// CHECK_STDERR: /  ctrl_c\.exe\!main\+0x[0-9a-f]+  \[.*\bctrl_c\.c @ 36\]/
// CHECK_EXIT_CODE: 0
