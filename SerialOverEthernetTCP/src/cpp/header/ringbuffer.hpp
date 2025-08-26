/*
 * ringbuffer.hpp
 *
 *  Created on: 26.08.2025
 *      Author: marvi
 */

#ifndef SRC_CPP_HEADER_RINGBUFFER_HPP_
#define SRC_CPP_HEADER_RINGBUFFER_HPP_

class Ringbuffer {

private:
	unsigned long int size;
	char* buffer;
	unsigned long int writeIndex;
	unsigned long int readIndex;

public:
	Ringbuffer(unsigned long int size);
	~Ringbuffer();

	unsigned long int push(const char* data, unsigned long int length);
	unsigned long int dataAvailable() const;
	const char* dataStart() const;
	void pushRead(unsigned long int length);

};



#endif /* SRC_CPP_HEADER_RINGBUFFER_HPP_ */
