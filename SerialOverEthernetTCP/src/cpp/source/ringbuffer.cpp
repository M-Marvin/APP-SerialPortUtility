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
	this->buffer = new char[size];
	this->writeIndex = this->readIndex = 0;
}

Ringbuffer::~Ringbuffer()
{
	delete this->buffer;
}

unsigned long int Ringbuffer::push(const char* data, unsigned long int length)
{
	unsigned int free = this->writeIndex < this->readIndex ?
			(this->readIndex - this->writeIndex) - 1 :
			this->readIndex + (this->size - this->writeIndex - 1);

	unsigned int transfer = free < length ? free : length;

	if (this->readIndex >= this->writeIndex) {
		std::memcpy(this->buffer + this->writeIndex, data, transfer);
	} else {
		unsigned long int partial = this->size - this->writeIndex;
		if (partial > transfer) partial = transfer;
		std::memcpy(this->buffer + this->writeIndex, data, partial);
		if (transfer > partial)
			std::memcpy(this->buffer, data + partial, transfer - partial);
	}

	this->writeIndex = (this->writeIndex + transfer) % this->size;

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
			this->size - this->readIndex;
}

void Ringbuffer::pushRead(unsigned long int length)
{
	this->readIndex = (this->readIndex + length) % this->size;
}
