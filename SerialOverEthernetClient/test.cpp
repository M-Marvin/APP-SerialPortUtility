/*
 * test.cpp
 *
 *  Created on: 12.02.2025
 *      Author: marvi
 */

#include <map>
#include <stdio.h>

#include "includeheader.hpp"

int main(int argc, const char** argv) {

	SomeClass* myClass = newImplInstance();

	SomeDataType* myData = newImplData();

	myClass->function(*myData);

	return 0;
}
