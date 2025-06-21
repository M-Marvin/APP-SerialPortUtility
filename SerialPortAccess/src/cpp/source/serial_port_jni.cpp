#ifdef INCLUDE_JNIAPI

#include "serial_port.hpp"
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <de_m_marvin_serialportaccess_SerialPort.h>
#include <jni.h>
#include <string>

using namespace std;

JNIEXPORT jlong JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1createSerialPort(JNIEnv* env, jclass clazz, jstring portName)
{
	SerialPort* port = newSerialPort(env->GetStringUTFChars(portName, 0));
	return (jlong)port;
}

JNIEXPORT void JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1disposeSerialPort(JNIEnv* env, jclass clazz, jlong handle)
{
	SerialPort* port = (SerialPort*)handle;
	delete port;
}

JNIEXPORT jboolean JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1setBaud(JNIEnv* env, jclass clazz, jlong handle, jint baud)
{
	SerialPort* port = (SerialPort*)handle;
	return port->setBaud(baud);
}

JNIEXPORT jint JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1getBaud(JNIEnv* env, jclass clazz, jlong handle)
{
	SerialPort* port = (SerialPort*)handle;
	return port->getBaud();
}

jclass FindClass(JNIEnv* env, const char* className)
{
	jclass clazz = env->FindClass(className);
	return clazz;
}

jfieldID FindField(JNIEnv* env, jclass clazz, const char* fieldName, const char* fieldSignature) {
	if (clazz == 0) return 0;
	jfieldID field = env->GetFieldID(clazz, fieldName, fieldSignature);
	return field;
}

jmethodID FindMethod(JNIEnv* env, jclass clazz, const char* methodName, const char* methodSignature) {
	if (clazz == 0) return 0;
	jmethodID method = env->GetStaticMethodID(clazz, methodName, methodSignature);
	return method;
}

jobject GetEnum(JNIEnv* env, jclass enumClazz, jmethodID enumMethod, int value) {
	if (enumClazz == 0) return 0;
	if (enumMethod == 0) return 0;
	return env->CallStaticObjectMethod(enumClazz, enumMethod, (jint) value);
}

JNIEXPORT jboolean JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1setConfig(JNIEnv* env, jclass clazz, jlong handle, jobject config)
{
	if (config == 0) return false;

	SerialPort* port = (SerialPort*)handle;
	SerialPortConfiguration configuration;

	jclass configClass = FindClass(env, "de/m_marvin/serialportaccess/SerialPort$SerialPortConfiguration");
	jfieldID baudRateField = FindField(env, configClass, "baudRate", "J");
	jfieldID dataBitsField = FindField(env, configClass, "dataBits", "B");
	jfieldID stopBitsField = FindField(env, configClass, "stopBits", "Lde/m_marvin/serialportaccess/SerialPort$SerialPortStopBits;");
	jfieldID parityField = FindField(env, configClass, "parity", "Lde/m_marvin/serialportaccess/SerialPort$SerialPortParity;");
	jfieldID flowControlField = FindField(env, configClass, "flowControl", "Lde/m_marvin/serialportaccess/SerialPort$SerialPortFlowControl;");
	jclass stopBitsClass = FindClass(env, "de/m_marvin/serialportaccess/SerialPort$SerialPortStopBits");
	jfieldID stopBitsValueField = FindField(env, stopBitsClass, "value", "I");
	jclass parityClass = FindClass(env, "de/m_marvin/serialportaccess/SerialPort$SerialPortParity");
	jfieldID parityValueField = FindField(env, parityClass, "value", "I");
	jclass flowControlClass = FindClass(env, "de/m_marvin/serialportaccess/SerialPort$SerialPortFlowControl");
	jfieldID flowControlValueField = FindField(env, flowControlClass, "value", "I");

	if (baudRateField == 0 || dataBitsField == 0 || stopBitsValueField == 0 || parityValueField == 0 || flowControlValueField == 0) return false;

	configuration.baudRate = (unsigned long) env->GetLongField(config, baudRateField);
	configuration.dataBits = (unsigned char) env->GetByteField(config, dataBitsField);
	jobject stopBits = env->GetObjectField(config, stopBitsField);
	if (stopBits == 0) return false;
	configuration.stopBits = static_cast<SerialPortStopBits>(env->GetIntField(stopBits, stopBitsValueField));
	jobject parity = env->GetObjectField(config, parityField);
	if (parity == 0) return false;
	configuration.parity = static_cast<SerialPortParity>(env->GetIntField(parity, parityValueField));
	jobject flowControl = env->GetObjectField(config, flowControlField);
	if (flowControl == 0) return false;
	configuration.flowControl = static_cast<SerialPortFlowControl>(env->GetIntField(flowControl, flowControlValueField));

	return port->setConfig(configuration);
}

JNIEXPORT jboolean JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1getConfig(JNIEnv* env, jclass clazz, jlong handle, jobject config)
{
	if (config == 0) return false;

	SerialPort* port = (SerialPort*)handle;
	SerialPortConfiguration configuration;

	if (!port->getConfig(configuration)) return false;

	jclass configClass = FindClass(env, "de/m_marvin/serialportaccess/SerialPort$SerialPortConfiguration");
	jfieldID baudRateField = FindField(env, configClass, "baudRate", "J");
	jfieldID dataBitsField = FindField(env, configClass, "dataBits", "B");
	jfieldID stopBitsField = FindField(env, configClass, "stopBits", "Lde/m_marvin/serialportaccess/SerialPort$SerialPortStopBits;");
	jfieldID parityField = FindField(env, configClass, "parity", "Lde/m_marvin/serialportaccess/SerialPort$SerialPortParity;");
	jfieldID flowControlField = FindField(env, configClass, "flowControl", "Lde/m_marvin/serialportaccess/SerialPort$SerialPortFlowControl;");
	jclass stopBitsClass = FindClass(env, "de/m_marvin/serialportaccess/SerialPort$SerialPortStopBits");
	jmethodID stopBitsValueMethod = FindMethod(env, stopBitsClass, "fromValue", "(I)Lde/m_marvin/serialportaccess/SerialPort$SerialPortStopBits;");
	jclass parityClass = FindClass(env, "de/m_marvin/serialportaccess/SerialPort$SerialPortParity");
	jmethodID parityValueMethod = FindMethod(env, parityClass, "fromValue", "(I)Lde/m_marvin/serialportaccess/SerialPort$SerialPortParity;");
	jclass flowControlClass = FindClass(env, "de/m_marvin/serialportaccess/SerialPort$SerialPortFlowControl");
	jmethodID flowControlValueMethod = FindMethod(env, flowControlClass, "fromValue", "(I)Lde/m_marvin/serialportaccess/SerialPort$SerialPortFlowControl;");

	if (baudRateField == 0 || dataBitsField == 0 || stopBitsValueMethod == 0 || parityValueMethod == 0 || flowControlValueMethod == 0) return false;

	env->SetLongField(config, baudRateField, (jlong) configuration.baudRate);
	env->SetByteField(config, dataBitsField, (jbyte) configuration.dataBits);
	jobject stopBitsEnum = GetEnum(env, stopBitsClass, stopBitsValueMethod, configuration.stopBits);
	env->SetObjectField(config, stopBitsField, stopBitsEnum);
	jobject parityEnum = GetEnum(env, parityClass, parityValueMethod, (jint) configuration.parity);
	env->SetObjectField(config, parityField, parityEnum);
	jobject flowControlEnum = GetEnum(env, flowControlClass, flowControlValueMethod, (jint) configuration.flowControl);
	env->SetObjectField(config, flowControlField, flowControlEnum);

	return true;
}

JNIEXPORT jboolean JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1setTimeouts(JNIEnv* env, jclass clazz, jlong handle, jint readTimeout, jint writeTimeout)
{
	SerialPort* port = (SerialPort*)handle;
	return port->setTimeouts(readTimeout, writeTimeout);
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
	if (readBytes > 0) {
		jstring js =  env->NewStringUTF(readBuffer);
		free(readBuffer);
		return js;
	}
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
		free(readBuffer);
		return byteArr;
	}
	free(readBuffer);
	return 0;
}

JNIEXPORT jstring JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1readDataConsecutiveS(JNIEnv* env, jclass clazz, jlong handle, jint bufferCapacity, jlong consecutiveDelay, jlong receptionWaitTimeout)
{
	SerialPort* port = (SerialPort*)handle;
	char* readBuffer = (char*)malloc(bufferCapacity);
	if (readBuffer == 0) return 0;
	memset(readBuffer, 0, bufferCapacity);
	unsigned long readBytes = port->readBytesConsecutive(readBuffer, (unsigned long) bufferCapacity, (long long) consecutiveDelay, (long long) receptionWaitTimeout);
	if (readBytes > 0) {
		jstring js =  env->NewStringUTF(readBuffer);
		free(readBuffer);
		return js;
	}
	free(readBuffer);
	return 0;
}

JNIEXPORT jbyteArray JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1readDataConsecutiveB(JNIEnv* env, jclass clazz, jlong handle, jint bufferCapacity, jlong consecutiveDelay, jlong receptionWaitTimeout)
{
	SerialPort* port = (SerialPort*)handle;
	char* readBuffer = (char*)malloc(bufferCapacity);
	if (readBuffer == 0) return 0;
	memset(readBuffer, 0, bufferCapacity);
	unsigned long readBytes = port->readBytesConsecutive(readBuffer, (unsigned long) bufferCapacity, (long long) consecutiveDelay, (long long) receptionWaitTimeout);
	if (readBytes > 0)
	{
		jbyteArray byteArr = env->NewByteArray(readBytes);
		env->SetByteArrayRegion(byteArr, 0, readBytes, (jbyte*)readBuffer);
		free(readBuffer);
		return byteArr;
	}
	free(readBuffer);
	return 0;
}

JNIEXPORT jint JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1writeDataS(JNIEnv* env, jclass clazz, jlong handle, jstring data)
{
	SerialPort* port = (SerialPort*)handle;
	const char* writeBuffer = env->GetStringUTFChars(data, 0);
	unsigned long bufferLength = env->GetStringUTFLength(data);
	return port->writeBytes(writeBuffer, bufferLength);
}

JNIEXPORT jint JNICALL Java_de_m_1marvin_serialportaccess_SerialPort_n_1writeDataB(JNIEnv* env, jclass clazz, jlong handle, jbyteArray data)
{
	SerialPort* port = (SerialPort*)handle;
	const char* writeBuffer = (char*)env->GetByteArrayElements(data, 0);
	unsigned long bufferLength = env->GetArrayLength(data);
	return port->writeBytes(writeBuffer, bufferLength);
}

#endif
