#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <jvmti.h>
#include <jvmticmlr.h>

static jvmtiEnv *jvmti = NULL;
static jvmtiCapabilities capabilities;
jrawMonitorID lock;

struct sockaddr_in server, client;

static void print_jvmti_error(jvmtiEnv *jvmti, jvmtiError errnum, const char *str) {
	char *errnum_str;
	const char *msg_str = str == NULL ? "" : str;
	char *msg_err = NULL;
	errnum_str = NULL;

	(void)(*jvmti)->GetErrorName(jvmti, errnum, &errnum_str);
	msg_err = errnum_str == NULL ? "Unkown" : errnum_str;
	printf("Error: JVM TI: %d(%s): %s\n", errnum, msg_err, msg_str);
}

static void check_jvmti_error(jvmtiEnv *jvmti, jvmtiError errnum, const char *str) {
	if (errnum != JVMTI_ERROR_NONE) {
		print_jvmti_error(jvmti, errnum, str);
	}
}

static void enter_critical_section(jvmtiEnv *jvmti) {
	jvmtiError error;

	error = (*jvmti)->RawMonitorEnter(jvmti, lock);
	check_jvmti_error(jvmti, error, "Cannot enter Raw Monitor");
}

static void exit_critical_section(jvmtiEnv *jvmti) {
	jvmtiError error;

	error = (*jvmti)->RawMonitorExit(jvmti, lock);
	check_jvmti_error(jvmti, error, "Cannot exit Raw Monitor");
}

static void JNICALL callback_on_gc_start(jvmtiEnv *jvmti_env) {
	enter_critical_section(jvmti);
	printf("**********GC start\n");
	exit_critical_section(jvmti);
}

static void JNICALL callback_on_gc_finish(jvmtiEnv *jvmti_env) {
	enter_critical_section(jvmti);
	printf("**********GC finished\n");
	exit_critical_section(jvmti);
}

jvmtiError set_capabilities() {
	jvmtiError error;

	(void)memset(&capabilities, 0, sizeof(jvmtiCapabilities));
	capabilities.can_generate_garbage_collection_events = 1;

	error = (*jvmti)->AddCapabilities(jvmti, &capabilities);
	check_jvmti_error(jvmti, error, "Unable to get necessary JVM TI capabilities.");
	return error;
}

jvmtiError register_all_callback_functions() {
	jvmtiEventCallbacks callbacks;
	jvmtiError error;

	(void)memset(&callbacks, 0, sizeof(callbacks));

	callbacks.GarbageCollectionStart = &callback_on_gc_start;
	callbacks.GarbageCollectionFinish = &callback_on_gc_finish;

	error = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, (jint)sizeof(callbacks));
	check_jvmti_error(jvmti, error, "Cannot set JVM TI callbacks");
	return error;
}

jvmtiError set_event_notification_mode(int event) {
	jvmtiError error;

	error = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, event, (jthread)NULL);
	check_jvmti_error(jvmti, error, "Cannot set event notification");
	return error;
}

jvmtiError set_event_notification_modes() {
	jvmtiError error;

	if ((error = set_event_notification_mode(JVMTI_EVENT_GARBAGE_COLLECTION_START)) != JNI_OK) {
		return error;
	}

	if ((error = set_event_notification_mode(JVMTI_EVENT_GARBAGE_COLLECTION_FINISH)) != JNI_OK) {
		return error;
	}

	return error;
}

jvmtiError create_raw_monitor() {
	jvmtiError error;

	error = (*jvmti)->CreateRawMonitor(jvmti, "agent data", &lock);
	check_jvmti_error(jvmti, error, "Cannot create Raw Monitor");

	return error;
}

jvmtiError print_jvmti_version() {
	jvmtiError error;
	jint version;
	jint cmajor, cminor, cmicro;

	error = (*jvmti)->GetVersionNumber(jvmti, &version);

	cmajor = (version & JVMTI_VERSION_MASK_MAJOR) >> JVMTI_VERSION_SHIFT_MAJOR;
	cminor = (version & JVMTI_VERSION_MASK_MINOR) >> JVMTI_VERSION_SHIFT_MINOR;
	cmicro = (version & JVMTI_VERSION_MASK_MICRO) >> JVMTI_VERSION_SHIFT_MICRO;

	printf("Compile Time JVM TI Version: %d.%d.%d (0x%08x)\n", cmajor, cminor, cmicro, version);

	return error;
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *jvm, char *options, void *reserved) {
	jvmtiError error = JVMTI_ERROR_NONE;
	jint result;
	printf("Loading agent jGCScout\n");

	result = (*jvm)->GetEnv(jvm, (void **) &jvmti, JVMTI_VERSION_1_0);
	if (result != JNI_OK || jvmti == NULL) {
		printf("ERROR: Unable to access JVM TI Version 1 (0x%x),"
			" is your J2SE a 1.5 or newer version? JNIEnv's GetEnv() returned %d which is wrong.\n", 
			JVMTI_VERSION_1, (int)result);
		return result;
	}

	print_jvmti_version();

	if ((error = set_capabilities()) != JNI_OK) {
		return error;
	}

	if ((error = register_all_callback_functions()) != JNI_OK) {
		return error;
	}

	if ((error = set_event_notification_modes()) != JNI_OK) {
		return error;
	}

	if ((error = create_raw_monitor()) != JNI_OK) {
		return error;
	}

	return JNI_OK;
}

