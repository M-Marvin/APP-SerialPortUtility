
#include "pch.h"
#include <malloc.h>
#include <de_m_marvin_serialportaccess_SerialPort.h>
#include "serial_port.h"

JNIEXPORT jlong JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1createSerialPort(JNIEnv* env, jclass clazz, jstring portName)
{
	SerialPort* port = new SerialPort(env->GetStringUTFChars(portName, 0));
	return (jlong)port;
}

JNIEXPORT void JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1disposeSerialPort(JNIEnv* env, jclass clazz, jlong handle)
{
	SerialPort* port = (SerialPort*)handle;
	delete port;
}

JNIEXPORT void JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1setBaud(JNIEnv* env, jclass clazz, jlong handle, jint baud)
{
	SerialPort* port = (SerialPort*)handle;
	port->setBaud(baud);
}

JNIEXPORT jint JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1getBaud(JNIEnv* env, jclass clazz, jlong handle)
{
	SerialPort* port = (SerialPort*)handle;
	return port->getBaud();
}

JNIEXPORT void JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1setTimeouts(JNIEnv* env, jclass clazz, jlong handle, jint readTimeout, jint writeTimeout)
{
	SerialPort* port = (SerialPort*)handle;
	port->setTimeouts(readTimeout, writeTimeout);
}

JNIEXPORT jboolean JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1openPort(JNIEnv* env, jclass clazz, jlong handle)
{
	SerialPort* port = (SerialPort*)handle;
	return port->openPort();
}

JNIEXPORT void JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1closePort(JNIEnv* env, jclass clazz, jlong handle)
{
	SerialPort* port = (SerialPort*)handle;
	port->closePort();
}

JNIEXPORT jboolean JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1isOpen(JNIEnv* env, jclass clazz, jlong handle)
{
	SerialPort* port = (SerialPort*)handle;
	return port->isOpen();
}

JNIEXPORT jstring JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1readDataS(JNIEnv* env, jclass clazz, jlong handle, jint bufferCapacity)
{
	SerialPort* port = (SerialPort*)handle;
	char* readBuffer = (char*)malloc(bufferCapacity);
	if (readBuffer == 0) return 0;
	memset(readBuffer, 0, bufferCapacity);
	unsigned long readBytes = port->readBytes(readBuffer, (unsigned long) bufferCapacity);
	if (readBytes > 0) return env->NewStringUTF(readBuffer);
	free(readBuffer);
	return 0;
}

JNIEXPORT jbyteArray JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1readDataB(JNIEnv* env, jclass clazz, jlong handle, jint bufferCapacity)
{
	SerialPort* port = (SerialPort*)handle;
	char* readBuffer = (char*)malloc(bufferCapacity);
	if (readBuffer == 0) return 0;
	memset(readBuffer, 0, bufferCapacity);
	unsigned long readBytes = port->readBytes(readBuffer, (unsigned long) bufferCapacity);
	if (readBytes > 0)
	{
		jbyteArray byteArr = env->NewByteArray(readBytes);
		env->SetByteArrayRegion(byteArr, 0, readBytes, (jbyte*)readBuffer);
		return byteArr;
	}
	return 0;
}

JNIEXPORT jstring JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1readDataBurstS(JNIEnv* env, jclass clazz, jlong handle, jint bufferCapacity, jlong receptionLoopDelay)
{
	SerialPort* port = (SerialPort*)handle;
	char* readBuffer = (char*)malloc(bufferCapacity);
	if (readBuffer == 0) return 0;
	memset(readBuffer, 0, bufferCapacity);
	unsigned long readBytes = port->readBytesBurst(readBuffer, (unsigned long) bufferCapacity, (long long) receptionLoopDelay);
	if (readBytes > 0) return env->NewStringUTF(readBuffer);
	free(readBuffer);
	return 0;
}

JNIEXPORT jbyteArray JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1readDataB(JNIEnv* env, jclass clazz, jlong handle, jint bufferCapacity, jlong receptionLoopDelay)
{
	SerialPort* port = (SerialPort*)handle;
	char* readBuffer = (char*)malloc(bufferCapacity);
	if (readBuffer == 0) return 0;
	memset(readBuffer, 0, bufferCapacity);
	unsigned long readBytes = port->readBytesBurst(readBuffer, (unsigned long) bufferCapacity, (long long) receptionLoopDelay);
	if (readBytes > 0)
	{
		jbyteArray byteArr = env->NewByteArray(readBytes);
		env->SetByteArrayRegion(byteArr, 0, readBytes, (jbyte*)readBuffer);
		return byteArr;
	}
	return 0;
}

JNIEXPORT void JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1writeDataS(JNIEnv* env, jclass clazz, jlong handle, jstring data)
{
	SerialPort* port = (SerialPort*)handle;
	const char* writeBuffer = env->GetStringUTFChars(data, 0);
	unsigned long bufferLength = env->GetStringUTFLength(data);
	port->writeBytes(writeBuffer, bufferLength);
}

JNIEXPORT void JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1writeDataB(JNIEnv* env, jclass clazz, jlong handle, jbyteArray data)
{
	SerialPort* port = (SerialPort*)handle;
	const char* writeBuffer = (char*)env->GetByteArrayElements(data, 0);
	unsigned long bufferLength = env->GetArrayLength(data);
	port->writeBytes(writeBuffer, bufferLength);
}
