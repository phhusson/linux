#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <tee_client_api.h>

#if 0
TA_DEBUG
  local_a0 = 0x7a7050be;
  local_9c = 0xb5f8;
  local_9a = 0x4c06;
  local_98 = 0x81;
  local_97 = 0xca;
  local_96 = 0x52;
  local_95 = 0x3a;
  local_94 = 0xb2;
  local_93 = 2;
  local_92 = 0xa3;
  local_91 = 0x8a;
#endif
#define TA_DEBUG \
{ 0x7a7050be, 0xb5f8, 0x4c06, {0x81, 0xca, 0x52, 0x3a, 0xb2, 0x02, 0xa3, 0x8a} }

#define TA_VIDEO_FW \
{ 0x526fc4fc, 0x7ee6, 0x4a12, {0x96, 0xe3, 0x83, 0xda, 0x95, 0x65, 0xbc, 0xe8} }

int main(void) {
	TEEC_Result res;
	TEEC_Context ctx;
	TEEC_Session sess;
	TEEC_UUID uuid = TA_DEBUG;
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
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INOUT, TEEC_NONE,
			TEEC_NONE, TEEC_NONE);
	char buf[0x1800];
	memset(buf, 0, sizeof(buf));
	op.params[0].tmpref.buffer = (void*)buf;
	op.params[0].tmpref.size = sizeof(buf);

	//2 = rsv_mem_state
	//5 = get_sys_info
	//3 = get_hwcrypto_info
	//4 = ARB MVN status
	res = TEEC_InvokeCommand(&sess, 2, &op,
			&err_origin);
	if (res != TEEC_SUCCESS) {
		fprintf(stderr, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x\n",
				res, err_origin);
		return 1;
	}

	fprintf(stderr, "bip %d\n", op.params[0].tmpref.size);
	write(1, buf, op.params[0].tmpref.size);

	return 0;
}
