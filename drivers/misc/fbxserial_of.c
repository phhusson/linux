#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/fbxserial.h>
#include <linux/random.h>

static struct fbx_serial serial_info;

const struct fbx_serial *arch_get_fbxserial(void)
{
	return &serial_info;
}

EXPORT_SYMBOL(arch_get_fbxserial);

/*
 *
 */
static __init int fbxserial_of_read(void)
{
	struct device_node *np;
	const void *fbxserial_data;
	int len;

	np = of_find_node_by_path("/chosen");
	if (!np)
		return 0;

	fbxserial_data = of_get_property(np, "fbx,serialinfo", &len);
	fbxserialinfo_read(fbxserial_data, &serial_info);
	add_device_randomness(&serial_info, sizeof (serial_info));

	return 0;
}

arch_initcall(fbxserial_of_read);
