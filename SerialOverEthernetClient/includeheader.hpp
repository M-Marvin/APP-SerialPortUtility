/*
 * includeheader.hpp
 *
 *  Created on: 28.02.2025
 *      Author: marvi
 */

#ifndef INCLUDEHEADER_HPP_
#define INCLUDEHEADER_HPP_

struct SomeDataType { virtual ~SomeDataType() {} };

class SomeClass {

public:
	virtual void function(SomeDataType& data) = 0;

};

SomeClass* newImplInstance();
SomeDataType* newImplData();

#endif /* INCLUDEHEADER_HPP_ */
