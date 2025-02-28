/*
 * implementation.cpp
 *
 *  Created on: 28.02.2025
 *      Author: marvi
 */

#include "implheader.hpp"
#include <stdio.h>

SomeClassImpl::SomeClassImpl() {};
SomeClassImpl::~SomeClassImpl() {};

void SomeClassImpl::function(SomeDataType& data) {
	SomeDataTypeImpl& dataImpl = dynamic_cast<SomeDataTypeImpl&>(data);
	printf("Some Variable: %u\n", dataImpl.someVariable);
}
