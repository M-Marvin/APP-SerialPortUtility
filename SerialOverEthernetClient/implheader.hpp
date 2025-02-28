/*
 * implheader.hpp
 *
 *  Created on: 28.02.2025
 *      Author: marvi
 */

#ifndef IMPLHEADER_HPP_
#define IMPLHEADER_HPP_

#include "includeheader.hpp"

struct SomeDataTypeImpl : public SomeDataType {
	char someVariable;
};

class SomeClassImpl : public SomeClass {

public:
	SomeClassImpl();
	~SomeClassImpl();

	void function(SomeDataType& data);

};


SomeClass* newImplInstance() {
	return new SomeClassImpl();
}
SomeDataType* newImplData() {
	SomeDataTypeImpl* data = new SomeDataTypeImpl();
	data->someVariable = 12;
	return data;
}


#endif /* IMPLHEADER_HPP_ */
