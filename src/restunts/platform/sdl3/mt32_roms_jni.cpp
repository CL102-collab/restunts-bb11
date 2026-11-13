#include <jni.h>
#include "mt32_roms.h"

extern "C" JNIEXPORT jstring JNICALL Java_org_restunts_android_Mt32RomLibrary_inventoryNative(
	JNIEnv *environment, jclass, jstring directory)
{
	if (directory == nullptr) {
		return nullptr;
	}
	const legacy_char *path = environment->GetStringUTFChars(directory, nullptr);
	if (path == nullptr) {
		return nullptr;
	}
	legacy_char *inventory = mt32_rom_inventory_json(path);
	environment->ReleaseStringUTFChars(directory, path);
	if (inventory == nullptr) {
		return nullptr;
	}
	jstring result = environment->NewStringUTF(inventory);
	mt32_rom_inventory_free(inventory);
	return result;
}
