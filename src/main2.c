/*
    0xFFFF - Open Free Fiasco Firmware Flasher
    Copyright (C) 2012  Pali Rohár <pali.rohar@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "device.h"
#include "image.h"

#include "main2.h"

#define VERSION "0.6"

static void show_title(void) {
	printf("0xFFFF v%s  // The Free Fiasco Firmware Flasher\n", VERSION);
}

#define WITH_USB
//#define WITH_DEVICE

static void show_usage(void) {

	int i;
	printf (""

#if defined(WITH_USB) && ! defined(WITH_DEVICE)
		"Over USB:\n"
		" -b cmdline      boot default or loaded kernel with cmdline, empty use default\n"
		" -r              reboot device\n"
		" -l              load all specified images to RAM\n"
		" -f              flash all specified images\n"
		" -c              cold flash 2nd and secondary image and normal flash all others\n"
		"\n"
#endif

#ifdef WITH_DEVICE
		"On device:\n"
		" -r              reboot device\n"
		" -f              flash all specified images\n"
		" -x /dev/mtd     check for bad blocks on mtd device\n"
		" -E file         dump all images from device to one fiasco image, see -t\n"
		" -e dir          dump all images from device to directory, see -t\n"
		"\n"
#endif

#if defined(WITH_USB) || defined(WITH_DEVICE)
		"Device configuration:\n"
		" -I              identify, show all information about device\n"
		" -D 0|1|2        change root device: 0 - flash, 1 - mmc, 2 - usb\n"
		" -U 0|1          disable/enable USB host mode\n"
		" -R 0|1          disable/enable R&D mode\n"
		" -F flags        change R&D flags, flags are comma separated list, can be empty\n"
		" -H rev          change HW revision\n"
		" -K ver          change kernel version string\n"
		" -N ver          change NOLO version string\n"
		" -S ver          change SW release version string\n"
		" -C ver          change content eMMC version string\n"
		"\n"
#endif

		"Input image specification:\n"
		" -M file         specify fiasco image\n"
		" -m arg          specify normal image\n"
		"                 arg is [[[dev:[hw:]]ver:]type:]file[%%lay]\n"
		"                   dev is device name string (default: emtpy)\n"
		"                   hw are comma separated list of HW revisions (default: empty)\n"
		"                   ver is image version string (default: empty)\n"
		"                   type is image type (default: try autodetect)\n"
		"                   file is image file name\n"
		"                   lay is layout file name (default: none)\n"
		"\n"

		"Image filters:\n"
		" -t types        filter images by comma separated list of image types\n"
		" -d dev          filter images by device\n"
		" -w hw           filter images by HW revision\n"
		"\n"

		"Fiasco image:\n"
		" -u dir          unpack fiasco image to directory\n"
		" -g file[%%sw]    generate fiasco image to file with SW release version sw (default: without SW release)\n"
		"\n"

		"Other options:\n"
		" -i              identify images\n"
#if defined(WITH_USB) || defined(WITH_DEVICE)
		" -p              console prompt mode\n"
#endif
#if ( defined(WITH_USB) || defined(WITH_DEVICE) ) && defined(WITH_SQUEUES)
		" -Q              enter shared queues server mode (for gui or remote)\n"
#endif
		" -s              simulate, do not flash or write on disk\n"
		" -n              disable hash, checksum and image type checking\n"
		" -v              be verbose and noisy\n"
		" -h              show this help message\n"
		"\n"

#if defined(WITH_USB) || defined(WITH_DEVICE)
		"R&D flags:\n"
		"  no-omap-wd          disable auto reboot by OMAP watchdog\n"
		"  no-ext-wd           disable auto reboot by external watchdog\n"
		"  no-lifeguard-reset  disable auto reboot by software lifeguard\n"
		"  serial-console      enable serial console\n"
		"  no-usb-timeout      disable usb timeout for flashing\n"
		"  sti-console         enable sti console\n"
		"  no-charging         disable battery charging\n"
		"  force-power-key     force omap boot reason to power key\n"
		"\n"
#endif

	);

	printf( "Devices:\n");
	for ( i = 0; i < DEVICE_COUNT; ++i )
		if ( device_to_string(i) && device_to_long_string(i) )
			printf("  %-14s %s\n", device_to_string(i), device_to_long_string(i));
	printf( "\n");

	printf( "Image types:\n");
	for ( i = 0; i < IMAGE_COUNT; ++i )
		if ( image_type_to_string(i) )
			printf("  %s\n", image_type_to_string(i));
	printf( "\n");

}

int simulate;
int noverify;
int verbose;

struct image_list * image_first = NULL;

/* arg = [[[dev:[hw:]]ver:]type:]file[%%lay] */
static void parse_image_arg(char * arg) {

	struct image * image;
	char * file;
	char * type;
	char * device;
	char * hwrevs;
	char * version;
	char * layout;
	char * layout_file;

	layout_file = strchr(arg, '%');
	if (layout_file)
		*(layout_file++) = 0;

	type = NULL;
	device = NULL;
	hwrevs = NULL;
	version = NULL;
	layout = NULL;

	file = strrchr(arg, ':');
	if (file) {
		*(file++) = 0;
		type = strrchr(arg, ':');
		if (type) {
			*(type++) = 0;
			version = strrchr(arg, ':');
			if (version) {
				*(version++) = 0;
				hwrevs = strchr(arg, ':');
				if (hwrevs)
					*(hwrevs++) = 0;
				device = arg;
			} else {
				version = arg;
			}
		} else {
			type = arg;
		}
	} else {
		file = arg;
	}

	if ( layout_file ) {
		off_t len;
		int fd = open(layout_file, O_RDONLY);
		if ( fd < 0 ) {
			fprintf(stderr, "Cannot open layout file %s: %s\n", layout_file, strerror(errno));
			exit(1);
		}
		len = lseek(fd, 0, SEEK_END);
		if ( len == (off_t)-1 ) {
			fprintf(stderr, "Cannot get file size\n");
			exit(1);
		}
		lseek(fd, 0, SEEK_SET);
		layout = malloc(len);
		if ( ! layout ) {
			fprintf(stderr, "Alloc error\n");
			exit(1);
		}
		if ( read(fd, layout, len) != len ) {
			fprintf(stderr, "Cannot read layout file %s: %s\n", layout_file, strerror(errno));
			exit(1);
		}
	}

	image = image_alloc_from_file(file, type, device, hwrevs, version, layout);

	if ( layout )
		free(layout);

	if ( ! image ) {
		fprintf(stderr, "Cannot load image file %s\n", file);
		exit(1);
	}

	image_list_add(&image_first, image);

}

int main(int argc, char **argv) {

	const char * optstring = ""
#if defined(WITH_USB) && ! defined(WITH_DEVICE)
	"b:rlfc"
#endif
#ifdef WITH_DEVICE
	"rfx:E:e:"
#endif
#if defined(WITH_USB) || defined(WITH_DEVICE)
	"ID:U:R:F:H:K:N:S:C:"
#endif
	"M:m:"
	"t:d:w:"
	"u:g:"
	"i"
#if defined(WITH_USB) || defined(WITH_DEVICE)
	"p"
#endif
#if ( defined(WITH_USB) || defined(WITH_DEVICE) ) && defined(WITH_SQUEUES)
	"Q"
#endif
	"snvVh"
	"";
	int c;

	int dev_boot = 0;
	char * dev_boot_arg = NULL;
	int dev_reboot = 0;
	int dev_load = 0;
	int dev_flash = 0;
	int dev_cold_flash = 0;
	int dev_check = 0;
	char * dev_check_arg = NULL;
	int dev_dump_fiasco = 0;
	char * dev_dump_fiasco_arg = NULL;
	int dev_dump = 0;
	char * dev_dump_arg = NULL;
	int dev_ident = 0;
	int set_root = 0;
	char * set_root_arg = NULL;
	int set_usb = 0;
	char * set_usb_arg = NULL;
	int set_rd = 0;
	char * set_rd_arg = NULL;
	int set_rd_flags = 0;
	char * set_rd_flags_arg = NULL;
	int set_hw = 0;
	char * set_hw_arg = NULL;
	int set_kernel = 0;
	char * set_kernel_arg = NULL;
	int set_nolo = 0;
	char * set_nolo_arg = NULL;
	int set_sw = 0;
	char * set_sw_arg = NULL;
	int set_emmc = 0;
	char * set_emmc_arg = NULL;
	int image_fiasco = 0;
	char * image_fiasco_arg = NULL;
	int filter_type = 0;
	char * filter_type_arg = NULL;
	int filter_device = 0;
	char * filter_device_arg = NULL;
	int filter_rev = 0;
	char * filter_rev_arg = NULL;
	int fiasco_unpack = 0;
	char * fiasco_unpack_arg = NULL;
	int fiasco_gen = 0;
	char * fiasco_gen_arg = NULL;
	int image_ident = 0;
	int console = 0;
	int queue = 0;

	simulate = 0;
	noverify = 0;
	verbose = 0;

	int help = 0;

	show_title();

	while ( ( c = getopt(argc, argv, optstring) ) != -1 ) {

		switch(c) {
			case '?':
				fprintf(stderr, "error ?\n");
				break;
			case 'b':
				dev_boot = 1;
				dev_boot_arg = optarg;
				break;
			case 'r':
				dev_reboot = 1;
				break;
			case 'l':
				dev_load = 1;
				break;
			case 'f':
				dev_flash = 1;
				break;
			case 'c':
				dev_cold_flash = 1;
				break;

			case 'x':
				dev_check = 1;
				dev_check_arg = optarg;
				break;
			case 'E':
				dev_dump_fiasco = 1;
				dev_dump_fiasco_arg = optarg;
				break;
			case 'e':
				dev_dump = 1;
				dev_dump_arg = optarg;
				break;

			case 'I':
				dev_ident = 1;
				break;
			case 'D':
				set_root = 1;
				set_root_arg = optarg;
				break;
			case 'U':
				set_usb = 1;
				set_usb_arg = optarg;
				break;
			case 'R':
				set_rd = 1;
				set_rd_arg = optarg;
				break;
			case 'F':
				set_rd_flags = 1;
				set_rd_flags_arg = optarg;
				break;
			case 'H':
				set_hw = 1;
				set_hw_arg = optarg;
				break;
			case 'K':
				set_kernel = 1;
				set_kernel_arg = optarg;
				break;
			case 'N':
				set_nolo = 1;
				set_nolo_arg = optarg;
				break;
			case 'S':
				set_sw = 1;
				set_sw_arg = optarg;
				break;
			case 'C':
				set_emmc = 1;
				set_emmc_arg = optarg;
				break;

			case 'M':
				image_fiasco = 1;
				image_fiasco_arg = optarg;
				break;
			case 'm':
				parse_image_arg(optarg);
				break;

			case 't':
				filter_type = 1;
				filter_type_arg = optarg;
				break;
			case 'd':
				filter_device = 1;
				filter_device_arg = optarg;
				break;
			case 'w':
				filter_rev = 1;
				filter_rev_arg = optarg;
				break;

			case 'u':
				fiasco_unpack = 1;
				fiasco_unpack_arg = optarg;
				break;
			case 'g':
				fiasco_gen = 1;
				fiasco_gen_arg = optarg;
				break;

			case 'i':
				image_ident = 1;
				break;
			case 'p':
				console = 1;
				break;
			case 'Q':
				queue = 1;
				break;

			case 's':
				simulate = 1;
				break;
			case 'n':
				noverify = 1;
				break;
			case 'v':
				verbose = 1;
				break;

			case 'h':
				help = 1;
				break;

			default:
				fprintf(stderr, "error c:%c\n", c);
				break;
		}

	}


	if ( help ) {
		show_usage();
		return 0;
	}

	/* console */
	if ( console ) {
//		console_prompt();
		return 0;
	}

	/* share queues */
	if ( queue ) {
//		queue_mode();
		return 0;
	}


	/* load images from files */

	if ( image_first && image_fiasco ) {
		fprintf(stderr, "Cannot specify together normal images and fiasco images\n");
		return 1;
	}

	/* filter images */

	/* identificate images */
	if ( image_ident ) {

	}

	/* unpack fiasco */

	/* generate fiasco */


	/* cold flash */

	/* flash */

	/* configuration */

	/* load */

	/* boot */

	/* reboot */


	/* check */

	/* dump */

	return 0;
}