#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/crc32.h>
#include <linux/slab.h>

#include <asm/io.h>

#include <linux/fbxserial.h>

#define PFX "builtin-fbxserial: "

static void __init
fbxserialinfo_use_default(struct fbx_serial *serial)
{
	printk(KERN_WARNING PFX "warning: using default serial infos\n");
	fbxserial_set_default(serial);
}

/*
 * add trailing 0 for bundle string here.
 */
static void __init
bundle_fixup(struct fbx_serial *serial)
{
	struct fbx_serial_extinfo *p;
	int i;

	for (i = 0; i < be32_to_cpu(serial->extinfo_count); i++) {

		if (i >= EXTINFO_MAX_COUNT)
			break;

		p = &serial->extinfos[i];
		if (be32_to_cpu(p->type) == EXTINFO_TYPE_EXTDEV &&
		    be32_to_cpu(p->u.extdev.type) == EXTDEV_TYPE_BUNDLE) {
			int size;

			size = sizeof (p->u.extdev.serial);
			p->u.extdev.serial[size - 1] = 0;
		}
	}
}

/*
 * called from  arch code early  in the boot sequence.   This function
 * returns 1  in case serial infos are  invalid/unreadable and default
 * values have been used.
 */
int __init
fbxserialinfo_read(const void *data, struct fbx_serial *out)
{
	uint32_t sum;

	if (!data) {
		printk(KERN_NOTICE PFX "no serial data\n");
		goto out_default;
	}

	/*
	 * get partial serial data from flash/whatever.
	 */
	memcpy(out, data, sizeof (*out));

	/* check magic first */
	if (be32_to_cpu(out->magic) != FBXSERIAL_MAGIC) {
		printk(KERN_NOTICE PFX "invalid magic (%08x, expected %08x), "
			"using defaults !\n", be32_to_cpu(out->magic),
		       FBXSERIAL_MAGIC);
		goto out_default;
	}

	/* fetch size for which we have to check CRC */
	if (be32_to_cpu(out->len) > FBXSERIAL_MAX_SIZE) {
		printk(KERN_NOTICE PFX "structure size too big (%d), "
		       "using defaults !\n", be32_to_cpu(out->len));
		goto out_default;
	}

	/* compute and check checksum */
	sum = crc32(0, data + 4, be32_to_cpu(out->len) - 4);

	if (be32_to_cpu(out->crc32) != sum) {
		printk(KERN_NOTICE PFX "invalid checksum (%08x, "
		       "expected %08x), using defaults !\n", sum,
		       be32_to_cpu(out->crc32));
		goto out_default;
	}

	printk(KERN_INFO PFX "Found valid serial infos !\n");
	bundle_fixup(out);
	return 0;

 out_default:
	fbxserialinfo_use_default(out);
	bundle_fixup(out);
	return 1;
}

void
fbxserialinfo_get_random(unsigned char *data, unsigned int len)
{
	const struct fbx_serial *s;

	memset(data, 0, 6);
	s = arch_get_fbxserial();
	if (WARN(!s, "arch_get_fbxserial returned NULL"))
		return;

	if (len > sizeof (s->random_data))
		len = sizeof (s->random_data);

	memcpy(data, s->random_data, len);
}
EXPORT_SYMBOL(fbxserialinfo_get_random);

static u8 *mac_table;

static void inc_mac(u8 *mac, int count)
{
	int index = 5;
	int overflow;

	do {
		unsigned int val = mac[index] + count;

		overflow = val >> 8;
		mac[index] = val;
		count = (count + 255) >> 8;
		--index;
	} while (index >= 0 && overflow);
}

static int gen_mac_table(const struct fbx_serial *s)
{
	int i;

	mac_table = kmalloc(6 * s->mac_count, GFP_KERNEL);
	if (!mac_table)
		return -ENOMEM;

	for (i = 0; i < s->mac_count; ++i) {
		u8 *mac = &mac_table[6 * i];

		memcpy(mac, s->mac_addr_base, 6);
		inc_mac(mac, i);
	}
	return 0;
}

const void *
fbxserialinfo_get_mac_addr(unsigned int index)
{
	const struct fbx_serial *s;

	s = arch_get_fbxserial();

	if (!s) {
		pr_warn(PFX "no serial available: using default.\n");
		goto default_mac;
	}

	if (index > s->mac_count) {
		pr_warn(PFX "mac index %d too high: using default.\n",
			index);
		goto default_mac;
	}

	if (!mac_table) {
		int error = gen_mac_table(s);
		if (error) {
			pr_err(PFX "gen_mac_table() failed: using default.\n");
			goto default_mac;
		}
	}

	return &mac_table[6 * index];

default_mac:
	 return "\x00\x07\xcb\x00\x00\xfd";
}
EXPORT_SYMBOL(fbxserialinfo_get_mac_addr);
