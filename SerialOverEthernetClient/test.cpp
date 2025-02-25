/*
 * test.cpp
 *
 *  Created on: 12.02.2025
 *      Author: marvi
 */

#include <map>
#include <stdio.h>

int main(int argc, const char** argv) {

	std::map<unsigned int, std::pair<unsigned long, char*>> stack;

	stack[2] = std::pair<unsigned long, char*>(123, new char[123] {0});

	std::pair<unsigned long, char*>& entry1 = stack[2];

	printf("key 2 = len %lu pointer %p\n", entry1.first, entry1.second);


	std::pair<unsigned long, char*>& entry2 = stack[3];

	printf("key 3 = len %lu pointer %p\n", entry2.first, entry2.second);

	entry2.first = 24;
	entry2.second = new char[24] {0};

	std::pair<unsigned long, char*>& entry3 = stack[3];

	printf("key 3 = len %lu pointer %p\n", entry3.first, entry3.second);

}
