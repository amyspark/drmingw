/**************************************************************************
 *
 * Copyright 2022 Jose Fonseca
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

#include <stdio.h>

int main()
{
#if defined(__MINGW32__) && !defined(__MINGW64__)
    return EXIT_SKIP;
#else
    char buf[8];
    sscanf("0123456789", "%s", buf);
    return 0;
#endif
}

// CHECK_STDERR: /  stack_buffer_overflow\.exe\!main\+0x[0-9a-f]+  \[.*\bstack_buffer_overflow\.c @ 39\]/
