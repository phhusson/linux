#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <tee_client_api.h>

#define TA_DEBUG \
{ 0x7a7050be, 0xb5f8, 0x4c06, {0x81, 0xca, 0x52, 0x3a, 0xb2, 0x02, 0xa3, 0x8a} }

#define TA_VIDEO_FW \
{ 0x526fc4fc, 0x7ee6, 0x4a12, {0x96, 0xe3, 0x83, 0xda, 0x95, 0x65, 0xbc, 0xe8} }

int main(void) {
	TEEC_Result res;
	TEEC_Context ctx;
	TEEC_Session sess;
	TEEC_UUID uuid = TA_VIDEO_FW;
	uint32_t err_origin;

	/* Initialize a context connecting us to the TEE */
	res = TEEC_InitializeContext(NULL, &ctx);
	if (res != TEEC_SUCCESS) {
		fprintf(stderr, "TEEC_InitializeContext failed with code 0x%x", res);
		return -1;
	}

	res = TEEC_OpenSession(&ctx, &sess, &uuid,
			TEEC_LOGIN_PUBLIC, NULL, NULL, &err_origin);
	if (res != TEEC_SUCCESS) {
		fprintf(stderr, "TEEC_Opensession failed with code 0x%x origin 0x%x\n",
				res, err_origin);
		return -2;
	}

	TEEC_Operation op;

	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_INPUT,
			TEEC_VALUE_INPUT, TEEC_NONE);

	int fd = open("video_ucode.bin", O_RDONLY);
	off_t fdSize = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	char *buf = malloc(fdSize);
	read(fd, buf, fdSize);
	close(fd);

	op.params[0].tmpref.buffer = (void*)(buf + 0x100);
	op.params[0].tmpref.size = fdSize - 0x100;
	op.params[1].tmpref.buffer = (void*)buf;
	op.params[1].tmpref.size = 0x100;
	op.params[2].value.a = 0;
	op.params[2].value.b = 0;

	res = TEEC_InvokeCommand(&sess, 0, &op,
			&err_origin);
	if (res != TEEC_SUCCESS) {
		fprintf(stderr, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x\n",
				res, err_origin);
		return 1;
	}

	return 0;
}
