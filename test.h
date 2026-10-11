/*
 * Copyright (c) 2026 a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _TEST_H
#define _TEST_H

#if TEST_MODE
void test_core0_task(void);
#else
static inline void test_core0_task(void) {}
#endif

#endif
