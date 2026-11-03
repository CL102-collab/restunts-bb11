#ifdef __ANDROID__
#include "../../c/renderer/graphics_backend.h"
#include <jni.h>
#include <stdio.h>
#include <string.h>

#define ANDROID_GRAPHICS_FIELDS_PER_DEVICE 2U
#define ANDROID_GRAPHICS_INDEX_CAPACITY 12U

/* Device names use UTF-8, whereas NewStringUTF expects modified UTF-8. Decode
 * their bytes with Java's UTF-8 constructor so non-ASCII names remain intact. */
static jstring graphics_android_name(JNIEnv *env, jclass string_class, jmethodID constructor,
									 jstring encoding, const legacy_char *name)
{
	jsize length = (jsize)strlen(name);
	jbyteArray bytes = (*env)->NewByteArray(env, length);
	if (bytes == NULL) {
		return NULL;
	}
	(*env)->SetByteArrayRegion(env, bytes, 0, length, (const jbyte *)name);
	jstring result = NULL;
	if (!(*env)->ExceptionCheck(env)) {
		result = (jstring)(*env)->NewObject(env, string_class, constructor, bytes, encoding);
	}
	(*env)->DeleteLocalRef(env, bytes);
	return result;
}

JNIEXPORT jobjectArray JNICALL
Java_org_restunts_android_VulkanDevices_queryNativeDevices(JNIEnv *env, jclass probe_class)
{
	(void)probe_class;
	struct GRAPHICS_DEVICE devices[GRAPHICS_DEVICE_CAPACITY];
	legacy_u32 count = graphics_enumerate_devices(devices, GRAPHICS_DEVICE_CAPACITY);
	if (count > GRAPHICS_DEVICE_CAPACITY) {
		count = GRAPHICS_DEVICE_CAPACITY;
	}
	jclass string_class = (*env)->FindClass(env, "java/lang/String");
	if (string_class == NULL) {
		return NULL;
	}
	jmethodID constructor =
		(*env)->GetMethodID(env, string_class, "<init>", "([BLjava/lang/String;)V");
	if (constructor == NULL) {
		return NULL;
	}
	jstring encoding = (*env)->NewStringUTF(env, "UTF-8");
	if (encoding == NULL) {
		return NULL;
	}
	jobjectArray result = (*env)->NewObjectArray(
		env, (jsize)(count * ANDROID_GRAPHICS_FIELDS_PER_DEVICE), string_class, NULL);
	if (result == NULL) {
		return NULL;
	}
	for (legacy_u32 index = 0; index < count; index++) {
		legacy_char number[ANDROID_GRAPHICS_INDEX_CAPACITY];
		(void)snprintf(number, sizeof(number), "%" LEGACY_PRId32, devices[index].index);
		devices[index].name[GRAPHICS_DEVICE_NAME_CAPACITY - 1U] = '\0';
		jstring device_index = (*env)->NewStringUTF(env, number);
		if (device_index == NULL) {
			return NULL;
		}
		jstring name =
			graphics_android_name(env, string_class, constructor, encoding, devices[index].name);
		if (name == NULL) {
			return NULL;
		}
		jsize field = (jsize)(index * ANDROID_GRAPHICS_FIELDS_PER_DEVICE);
		(*env)->SetObjectArrayElement(env, result, field, device_index);
		(*env)->SetObjectArrayElement(env, result, field + 1, name);
		(*env)->DeleteLocalRef(env, device_index);
		(*env)->DeleteLocalRef(env, name);
		if ((*env)->ExceptionCheck(env)) {
			return NULL;
		}
	}
	return result;
}
#endif
