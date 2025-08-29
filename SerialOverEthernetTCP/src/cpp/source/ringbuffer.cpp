/*
 * ringbuffer.cpp
 *
 *  Created on: 26.08.2025
 *      Author: marvi
 */

#include "ringbuffer.hpp"
#include <cstring>

Ringbuffer::Ringbuffer(unsigned long int size)
{
	this->size = size;
	this->buffer = new char[size * 2];
	this->writeIndex = this->readIndex = this->bufferEndIndex = 0;
}

Ringbuffer::~Ringbuffer()
{
	delete[] this->buffer;
}

unsigned long int Ringbuffer::push(const char* data, unsigned long int length)
{
	unsigned int free = this->writeIndex < this->readIndex ?
			(this->readIndex - this->writeIndex) - 1 :
			this->readIndex + (this->size - this->writeIndex - 1);

	unsigned int transfer = free < length ? free : length;
	std::memcpy(this->buffer + this->writeIndex, data, transfer);

	unsigned long int endOfBuffer = this->writeIndex + transfer;
	if (endOfBuffer > this->bufferEndIndex) this->bufferEndIndex = endOfBuffer;
	this->writeIndex = endOfBuffer % this->size;

	return transfer;
}

const char* Ringbuffer::dataStart() const
{
	return this->buffer + this->readIndex;
}

unsigned long int Ringbuffer::dataAvailable() const
{
	return this->readIndex <= this->writeIndex ?
			this->writeIndex - this->readIndex :
			this->bufferEndIndex - this->readIndex;
}

void Ringbuffer::pushRead(unsigned long int length)
{
	if (this->readIndex + length > this->size) {
		this->bufferEndIndex = this->writeIndex;
	}
	this->readIndex = (this->readIndex + length) % this->size;
}
