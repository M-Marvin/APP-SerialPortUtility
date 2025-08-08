/*
 * dbgprintf.h
 *
 * Debug print macro, enabled only when ENBL_DBGPRINT defined on compile time.
 * Provides additional debug information, which is not useful for production.
 *
 *  Created on: 01.07.2025
 *      Author: Marvin Koehler (M_Marvin)
 */

#ifndef SRC_CPP_HEADER_DBGPRINTF_H_
#define SRC_CPP_HEADER_DBGPRINTF_H_

#ifdef ENBL_DBGPRINT
#define dbgprintf(...) printf(__VA_ARGS__)
#else
#define dbgprintf(...)
#endif

#endif /* SRC_CPP_HEADER_DBGPRINTF_H_ */
