#ifndef FBXSERIAL_H_
#define FBXSERIAL_H_

#include <linux/kernel.h>
#include <linux/string.h>

/*
 * some part of serial may vary, we use abstract struct to store this,
 * data content depends on type.
 */
#define EXTINFO_SIZE		128
#define EXTINFO_MAX_COUNT	16

/*
 * extdev desc
 */
#define EXTINFO_TYPE_EXTDEV	1

#define EXTDEV_TYPE_BUNDLE	1
#define EXTDEV_TYPE_MAX		2

struct fbx_serial_extinfo {
	u32			type;

	union {
		/* extdev */
		struct {
			u32	type;
			u32	model;
			char	serial[64];
		} extdev;

		/* raw access */
		unsigned char	data[EXTINFO_SIZE];
	} u;
}  __attribute__ ((packed));;


/*
 * master serial structure
 */

#define FBXSERIAL_VERSION	1

#define FBXSERIAL_MAGIC		0x2d9521ab

#define MAC_ADDR_SIZE		6
#define RANDOM_DATA_SIZE	32

/*
 * this  is the  maximum size  we accept  to check  crc32  against, so
 * structure may no grow larger than this
 */
#define FBXSERIAL_MAX_SIZE	8192

struct fbx_serial {
	u32	crc32;
	u32	magic;
	u32	struct_version;
	u32	len;

	/* board serial */
	u16	type;
	u8	version;
	u8	manufacturer;
	u16	year;
	u8	week;
	u32	number;
	u32	flags;

	/* mac address base */
	u8	mac_addr_base[MAC_ADDR_SIZE];

	/* mac address count */
	u8	mac_count;

	/* random data */
	u8	random_data[RANDOM_DATA_SIZE];

	/* last update of data (seconds since epoch) */
	u32	last_modified;

	/* count of following extinfo tag */
	u32	extinfo_count;

	/* beginning of extended info */
	struct fbx_serial_extinfo	extinfos[EXTINFO_MAX_COUNT];

} __attribute__ ((packed));


/*
 * default value to use in case magic is wrong (no cksum in that case)
 */
static inline void fbxserial_set_default(struct fbx_serial *s)
{
	memset(s, 0, sizeof (*s));
	s->magic = FBXSERIAL_MAGIC;
	s->struct_version = FBXSERIAL_VERSION;
	s->len = sizeof (*s);
	s->manufacturer = '_';
	memcpy(s->mac_addr_base, "\x00\x07\xCB\x00\x00\xFD", 6);
	s->mac_count = 1;
}

void
fbxserialinfo_get_random(unsigned char *data, unsigned int len);

const void *
fbxserialinfo_get_mac_addr(unsigned int index);

int
fbxserialinfo_read(const void *data, struct fbx_serial *out);

struct fbx_serial *fbxserialinfo_get(void);

/*
 * implemented in board specific code
 */
#ifdef CONFIG_ARCH_HAS_FBXSERIAL
extern const struct fbx_serial *arch_get_fbxserial(void);
#else
static inline const struct fbx_serial *arch_get_fbxserial(void)
{
	return NULL;
}
#endif

#endif /* FBXSERIAL_H_ */
