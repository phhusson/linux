/* rc-rc6-freebox.c - Keytable for Freebox/Alicebox IR controller
 *
 * Copyright (c) 2012 by Nicolas Pouillon <npouillon@freebox.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>

#define RC6_FREEBOX_TV 0xf2
#define RC6_FREEBOX_WIDE 0x49
#define RC6_FREEBOX_POWER 0x0c
#define RC6_FREEBOX_STOP 0x31
#define RC6_FREEBOX_REC 0x37
#define RC6_FREEBOX_REW 0x2f
#define RC6_FREEBOX_PLAY 0xb8
#define RC6_FREEBOX_FF 0x2e
#define RC6_FREEBOX_PREV 0x4d
#define RC6_FREEBOX_NEXT 0x4c
#define RC6_FREEBOX_RELOAD 0x83
#define RC6_FREEBOX_MENU 0xcc
#define RC6_FREEBOX_UP 0x99
#define RC6_FREEBOX_LEFT 0x9b
#define RC6_FREEBOX_RIGHT 0x9c
#define RC6_FREEBOX_DOWN 0x9a
#define RC6_FREEBOX_OK 0x5c
#define RC6_FREEBOX_VOL_INC 0x5b
#define RC6_FREEBOX_VOL_DEC 0x5a
#define RC6_FREEBOX_MUTE 0x0d
#define RC6_FREEBOX_PROG_UP 0x58
#define RC6_FREEBOX_PROG_DOWN 0x59
#define RC6_FREEBOX_FREEBOX 0xd7
#define RC6_FREEBOX_RED 0x6d
#define RC6_FREEBOX_GREEN 0x6e
#define RC6_FREEBOX_YELLOW 0x6f
#define RC6_FREEBOX_BLUE 0x70
#define RC6_FREEBOX_1 0x01
#define RC6_FREEBOX_2 0x02
#define RC6_FREEBOX_3 0x03
#define RC6_FREEBOX_4 0x04
#define RC6_FREEBOX_5 0x05
#define RC6_FREEBOX_6 0x06
#define RC6_FREEBOX_7 0x07
#define RC6_FREEBOX_8 0x08
#define RC6_FREEBOX_9 0x09
#define RC6_FREEBOX_BACK 0x9e
#define RC6_FREEBOX_0 0x00
#define RC6_FREEBOX_SWAP 0x0a
#define RC6_FREEBOX_HELP 0x81
#define RC6_FREEBOX_INFO 0x0f
#define RC6_FREEBOX_GUIDE 0x97
#define RC6_FREEBOX_OPTIONS 0x54

#define MAP(x,y) { 0x80382600 + RC6_FREEBOX_##x, KEY_##y }

static struct rc_map_table rc6_freebox[] = {
	MAP(0, NUMERIC_0),
	MAP(1, NUMERIC_1),
	MAP(2, NUMERIC_2),
	MAP(3, NUMERIC_3),
	MAP(4, NUMERIC_4),
	MAP(5, NUMERIC_5),
	MAP(6, NUMERIC_6),
	MAP(7, NUMERIC_7),
	MAP(8, NUMERIC_8),
	MAP(9, NUMERIC_9),
	MAP(SWAP, BACK),
	MAP(POWER, POWER),
	MAP(MUTE, MUTE),
	MAP(INFO, INFO),

	MAP(FF, FASTFORWARD),
	MAP(REW, REWIND),
	MAP(STOP, STOP),
	MAP(REC, RECORD),
	MAP(WIDE, ZOOM),
	MAP(PREV, PREVIOUS),
	MAP(NEXT, NEXT),

	MAP(OPTIONS, OPTION),
	MAP(PROG_UP, CHANNELUP),
	MAP(PROG_DOWN, CHANNELDOWN),
	MAP(VOL_DEC, VOLUMEDOWN),
	MAP(VOL_INC, VOLUMEUP),
	MAP(OK, OK),

	MAP(RED, RED),
	MAP(GREEN, GREEN),
	MAP(YELLOW, YELLOW),
	MAP(BLUE, BLUE),

	MAP(HELP, HELP),
	MAP(RELOAD, REFRESH),

	MAP(GUIDE, PROGRAM),
	MAP(UP, UP),
	MAP(DOWN, DOWN),
	MAP(LEFT, LEFT),
	MAP(RIGHT, RIGHT),
	MAP(BACK, BACKSPACE),
	MAP(PLAY, PLAY),

	MAP(MENU, LIST),
	MAP(FREEBOX, HOME),
	MAP(TV, SCREEN),
};

static struct rc_map_list rc6_freebox_map = {
	.map = {
		.scan    = rc6_freebox,
		.size    = ARRAY_SIZE(rc6_freebox),
		.rc_proto = RC_PROTO_RC6_MCE,
		.name    = "rc-rc6-freebox",
	}
};

static int __init init_rc_map_rc6_freebox(void)
{
	return rc_map_register(&rc6_freebox_map);
}

static void __exit exit_rc_map_rc6_freebox(void)
{
	rc_map_unregister(&rc6_freebox_map);
}

module_init(init_rc_map_rc6_freebox)
module_exit(exit_rc_map_rc6_freebox)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nicolas Pouillon <npouillon@freebox.fr>");
