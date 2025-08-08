/*
 * misc.h
 *
 *  Created on: 01.07.2025
 *      Author: marvi
 */

#ifndef SRC_CPP_HEADER_DBGPRINTF_H_
#define SRC_CPP_HEADER_DBGPRINTF_H_

#ifdef ENBL_DBGPRINT
#define dbgprintf(...) printf(__VA_ARGS__)
#else
#define dbgprintf(...)
#endif

#endif /* SRC_CPP_HEADER_DBGPRINTF_H_ */
