/*
    0xFFFF - Open Free Fiasco Firmware Flasher
    Copyright (C) 2007, 2008  pancake <pancake@youterm.com>
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

#include "global.h"

#include "image.h"
#include "fiasco.h"
#include "device.h"
#include "usb-device.h"
#include "cold-flash.h"
#include "console.h"
#include "qmode.h"
#include "nolo.h"

static void show_title(void) {
	printf("0xFFFF v%s  // The Free Fiasco Firmware Flasher\n", VERSION);
}

static void show_usage(void) {

	int i;
	printf (""

#if defined(WITH_USB) && ! defined(WITH_DEVICE)
		"Over USB:\n"
		" -b [cmdline]    boot default or loaded kernel (if cmdline is empty use default, if cmdline starts with \"update\" boot to update mode)\n"
		" -r              reboot device\n"
		" -l              load kernel and initfs images to RAM\n"
		" -f              flash all specified images\n"
		" -c              cold flash 2nd and secondary image\n"
		"\n"
#endif

#ifdef WITH_DEVICE
		"On device:\n"
		" -r              reboot device\n"
		" -f              flash all specified images\n"
		" -x /dev/mtd     check for bad blocks on mtd device\n"
		" -E file         dump all images from device to one fiasco image, see -t\n"
		" -e [dir]        dump all images from device to directory, see -t (default: current directory)\n"
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
		" -N ver          change NOLO version string\n"
		" -K ver          change kernel version string\n"
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
		" -u [dir]        unpack fiasco image to directory (default: current directory)\n"
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

/* arg = [[[dev:[hw:]]ver:]type:]file[%%lay] */
static void parse_image_arg(char * arg, struct image_list ** image_first) {

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
			ERROR_INFO("Cannot open layout file %s", layout_file);
			exit(1);
		}
		len = lseek(fd, 0, SEEK_END);
		if ( len == (off_t)-1 ) {
			ERROR_INFO("Cannot get size of file %s", layout_file);
			exit(1);
		}
		lseek(fd, 0, SEEK_SET);
		layout = malloc(len);
		if ( ! layout ) {
			ALLOC_ERROR();
			exit(1);
		}
		if ( read(fd, layout, len) != len ) {
			ERROR_INFO("Cannot read %lu bytes from layout file %s", len, layout_file);
			exit(1);
		}
	}

	image = image_alloc_from_file(file, type, device, hwrevs, version, layout);

	if ( layout )
		free(layout);

	if ( ! image ) {
		ERROR("Cannot load image file %s", file);
		exit(1);
	}

	image_list_add(image_first, image);

}

void filter_images_by_type(enum image_type type, struct image_list ** image_first) {

	struct image_list * image_ptr = *image_first;
	while ( image_ptr ) {
		struct image_list * next = image_ptr->next;
		if ( image_ptr->image->type != type ) {
			image_list_del(image_ptr);
			if ( image_ptr == *image_first )
				*image_first = next;
		}
		image_ptr = next;
	}

}

void filter_images_by_device(enum device device, struct image_list ** image_first) {

	struct image_list * image_ptr = *image_first;
	while ( image_ptr ) {
		struct image_list * next = image_ptr->next;
		if ( image_ptr->image->device != device && image_ptr->image->device != DEVICE_ANY ) {
			image_list_del(image_ptr);
			if ( image_ptr == *image_first )
				*image_first = next;
		}
		image_ptr = next;
	}

}

void filter_images_by_hwrev(const char * hwrev, struct image_list ** image_first) {

	struct image_list * image_ptr = *image_first;
	while ( image_ptr ) {
		struct image_list * next = image_ptr->next;
		if ( ! image_hwrev_is_valid(image_ptr->image, hwrev) ) {
			image_list_del(image_ptr);
			if ( image_ptr == *image_first )
				*image_first = next;
		}
		image_ptr = next;
	}

}

int main(int argc, char **argv) {

	const char * optstring = ":"
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
	"snvh"
	"";
	int c;

	int ret = 0;

#if defined(WITH_USB) && ! defined(WITH_DEVICE)
	int dev_boot = 0;
	char * dev_boot_arg = NULL;
	int dev_load = 0;
	int dev_cold_flash = 0;
#endif

#ifdef WITH_DEVICE
	int dev_check = 0;
	char * dev_check_arg = NULL;
	int dev_dump_fiasco = 0;
	char * dev_dump_fiasco_arg = NULL;
	int dev_dump = 0;
	char * dev_dump_arg = NULL;
#endif

#if defined(WITH_USB) || defined(WITH_DEVICE)
	int dev_flash = 0;
	int dev_reboot = 0;
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
#endif

	int image_fiasco = 0;
	char * image_fiasco_arg = NULL;

	int filter_type = 0;
	char * filter_type_arg = NULL;
	int filter_device = 0;
	char * filter_device_arg = NULL;
	int filter_hwrev = 0;
	char * filter_hwrev_arg = NULL;

	int fiasco_un = 0;
	char * fiasco_un_arg = NULL;
	int fiasco_gen = 0;
	char * fiasco_gen_arg = NULL;

	int image_ident = 0;
#if defined(WITH_USB) || defined(WITH_DEVICE)
	int console = 0;
#endif
#if ( defined(WITH_USB) || defined(WITH_DEVICE) ) && defined(WITH_SQUEUES)
	int queue = 0;
#endif

	int help = 0;

	struct image_list * image_first = NULL;
	struct image_list * image_ptr = NULL;

	int have_2nd = 0;
	int have_secondary = 0;
	struct image * image_2nd = NULL;
	struct image * image_secondary = NULL;

	struct fiasco * fiasco_in = NULL;
	struct fiasco * fiasco_out = NULL;

	struct usb_device_info * usb_dev = NULL;

	char buf[512];

	simulate = 0;
	noverify = 0;
	verbose = 0;

	show_title();

	opterr = 0;

	while ( ( c = getopt(argc, argv, optstring) ) != -1 ) {

		switch (c) {

			default:
				ERROR("Unknown option '%c'", c);
				ret = 1;
				goto clean;

			case '?':
				ERROR("Unknown option '%c'", optopt);
				ret = 1;
				goto clean;

			case ':':
#if defined(WITH_USB) && ! defined(WITH_DEVICE)
				if ( optopt == 'b' ) {
					dev_boot = 1;
					break;
				}
#endif
#ifdef WITH_DEVICE
				if ( optopt == 'e' ) {
					dev_dump = 1;
					break;
				}
#endif
				if ( optopt == 'u' ) {
					fiasco_un = 1;
					break;
				}
				ERROR("Option '%c' requires an argument", optopt);
				ret = 1;
				goto clean;

#if defined(WITH_USB) && ! defined(WITH_DEVICE)
			case 'b':
				dev_boot = 1;
				if ( optarg[0] != '-' )
					dev_boot_arg = optarg;
				else
					--optind;
				break;
			case 'l':
				dev_load = 1;
				break;
			case 'c':
				dev_cold_flash = 1;
				break;
#endif

#ifdef WITH_DEVICE
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
				if ( optarg[0] != '-' )
					dev_dump_arg = optarg;
				else
					--optind;
				break;
#endif

#if defined(WITH_USB) || defined(WITH_DEVICE)
			case 'f':
				dev_flash = 1;
				break;
			case 'r':
				dev_reboot = 1;
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
#endif

			case 'M':
				image_fiasco = 1;
				image_fiasco_arg = optarg;
				break;
			case 'm':
				parse_image_arg(optarg, &image_first);
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
				filter_hwrev = 1;
				filter_hwrev_arg = optarg;
				break;

			case 'u':
				fiasco_un = 1;
				fiasco_un_arg = optarg;
				break;
			case 'g':
				fiasco_gen = 1;
				if ( optarg[0] != '-' )
					fiasco_gen_arg = optarg;
				else
					--optind;
				break;

			case 'i':
				image_ident = 1;
				break;
#if defined(WITH_USB) || defined(WITH_DEVICE)
			case 'p':
				console = 1;
				break;
#endif
#if ( defined(WITH_USB) || defined(WITH_DEVICE) ) && defined(WITH_SQUEUES)
			case 'Q':
				queue = 1;
				break;
#endif

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

		}

	}

	if ( optind < argc ) {
		ERROR("Extra argument '%s'", argv[optind]);
		ret = 1;
		goto clean;
	}

	/* help */
	if ( help ) {
		show_usage();
		ret = 0;
		goto clean;
	}

#if defined(WITH_USB) || defined(WITH_DEVICE)
	/* console */
	if ( console ) {
		console_prompt();
		ret = 0;
		goto clean;
	}
#endif

#if ( defined(WITH_USB) || defined(WITH_DEVICE) ) && defined(WITH_SQUEUES)
	/* share queues */
	if ( queue ) {
		queue_mode();
		ret = 0;
		goto clean;
	}
#endif


	/* load images from files */
	if ( image_first && image_fiasco ) {
		ERROR("Cannot specify normal and fiasco images together");
		ret = 1;
		goto clean;
	}

	/* load fiasco image */
	if ( image_fiasco ) {
		fiasco_in = fiasco_alloc_from_file(image_fiasco_arg);
		if ( ! fiasco_in )
			ERROR("Cannot load fiasco image file %s", image_fiasco_arg);
		else
			image_first = fiasco_in->first;
	}

	/* filter images by type */
	if ( filter_type ) {
		enum image_type type = image_type_from_string(filter_type_arg);
		if ( ! type )
			ERROR("Specified unknown image type for filtering: %s", filter_type_arg);
		else
			filter_images_by_type(type, &image_first);
	}

	/* filter images by device */
	if ( filter_device ) {
		enum device device = device_from_string(filter_device_arg);
		if ( ! device )
			ERROR("Specified unknown device for filtering: %s", filter_device_arg);
		else
			filter_images_by_device(device, &image_first);
	}

	/* filter images by hwrev */
	if ( filter_hwrev )
		filter_images_by_hwrev(filter_hwrev_arg, &image_first);

	/* reorder images for flashing (first x-loader, second secondary) */
	/* set 2nd and secondary images for cold-flashing */
	if ( dev_flash || dev_cold_flash ) {

		struct image_list * image_unorder_first;

		image_unorder_first = image_first;
		image_first = NULL;

		image_ptr = image_unorder_first;
		while ( image_ptr ) {
			struct image_list * next = image_ptr->next;
			if ( image_ptr->image->type == IMAGE_XLOADER ) {
				image_list_add(&image_first, image_ptr->image);
				image_list_unlink(image_ptr);
				free(image_ptr);
				if ( image_ptr == image_unorder_first )
					image_first = next;
			}
			image_ptr = next;
		}

		image_ptr = image_unorder_first;
		while ( image_ptr ) {
			struct image_list * next = image_ptr->next;
			if ( image_ptr->image->type == IMAGE_SECONDARY ) {
				if ( have_secondary == 0 ) {
					image_secondary = image_ptr->image;
					have_secondary = 1;
				} else if ( have_secondary == 1 ) {
					image_secondary = NULL;
					have_secondary = 2;
				}
				image_list_add(&image_first, image_ptr->image);
				image_list_unlink(image_ptr);
				free(image_ptr);
				if ( image_ptr == image_unorder_first )
					image_first = next;
			}
			image_ptr = next;
		}

		image_ptr = image_unorder_first;
		while ( image_ptr ) {
			struct image_list * next = image_ptr->next;
			if ( image_ptr->image->type == IMAGE_2ND ) {
				if ( have_2nd == 0 ) {
					image_2nd = image_ptr->image;
					have_2nd = 1;
				} else if ( have_2nd == 1 ) {
					image_2nd = NULL;
					have_2nd = 2;
				}
			}
			image_list_add(&image_first, image_ptr->image);
			image_list_unlink(image_ptr);
			free(image_ptr);
			if ( image_ptr == image_unorder_first )
				image_first = next;
			image_ptr = next;
		}

	}

	/* make sure that fiasco_in has valid images*/
	if ( fiasco_in )
		fiasco_in->first = image_first;

	/* identificate images */
	if ( image_ident ) {
		if ( fiasco_in ) {
			fiasco_print_info(fiasco_in);
			printf("\n");
		} else if ( ! image_first ) {
			ERROR("No image specified");
			ret = 1;
			goto clean;
		}
		for ( image_ptr = image_first; image_ptr; image_ptr = image_ptr->next ) {
			image_print_info(image_ptr->image);
			printf("\n");
		}
		ret = 0;
		goto clean;
	}

	/* unpack fiasco */
	if ( fiasco_un ) {
		if ( ! fiasco_in ) {
			ERROR("No fiasco image specified");
			ret = 1;
			goto clean;
		}
		fiasco_unpack(fiasco_in, fiasco_un_arg);
	}

	/* remove unknown images */
	image_ptr = image_first;
	while ( image_ptr ) {
		struct image_list * next = image_ptr->next;
		if ( image_ptr->image->type == IMAGE_UNKNOWN || image_ptr->image->device == DEVICE_UNKNOWN ) {
			WARNING("Removing unknown image (specified by %s %s)", image_ptr->image->orig_filename ? "file" : "fiasco", image_ptr->image->orig_filename ? image_ptr->image->orig_filename : "image");
			image_list_unlink(image_ptr);
			free(image_ptr);
			if ( image_ptr == image_first )
				image_first = next;
		}
		image_ptr = next;
	}

	/* make sure that fiasco_in has valid images */
	if ( fiasco_in )
		fiasco_in->first = image_first;

	/* generate fiasco */
	if ( fiasco_gen ) {
		char * swver = strchr(fiasco_gen_arg, '%');
		if ( swver )
			*(swver++) = 0;
		fiasco_out = fiasco_alloc_empty();
		if ( ! fiasco_out ) {
			ERROR("Cannot write images to fiasco file %s", fiasco_gen_arg);
		} else {
			if ( swver )
				strcpy(fiasco_out->swver, swver);
			fiasco_out->first = image_first;
			fiasco_write_to_file(fiasco_out, fiasco_gen_arg);
			fiasco_out->first = NULL;
			fiasco_free(fiasco_out);
		}
	}


#if defined(WITH_USB) && ! defined(WITH_DEVICE)

	/* over usb */

	if ( dev_cold_flash ) {
		if ( have_2nd == 0 ) {
			ERROR("2nd image for Cold Flashing was not specified");
			ret = 1;
			goto clean;
		} else if ( have_2nd == 2 ) {
			ERROR("More 2nd images for Cold Flashing was specified");
			ret = 1;
			goto clean;
		}

		if ( have_secondary == 0 ) {
			ERROR("Secondary image for Cold Flashing was not specified");
			ret = 1;
			goto clean;
		} else if ( have_secondary == 2 ) {
			ERROR("More Secondary images for Cold Flashing was specified");
			ret = 1;
			goto clean;
		}
	}

	if ( dev_load && dev_flash ) {
		ERROR("Options load and flash cannot de used together");
		ret = 1;
		goto clean;
	}

	if ( dev_boot || dev_reboot || dev_load || dev_flash || dev_cold_flash || dev_ident || set_root || set_usb || set_rd || set_rd_flags || set_hw || set_kernel || set_nolo || set_sw || set_emmc ) {

		do {

			usb_dev = usb_open_and_wait_for_device();

			/* cold flash */
			if ( dev_cold_flash ) {

				ret = cold_flash(usb_dev, image_2nd, image_secondary);
				usb_close_device(usb_dev);
				usb_dev = NULL;

				if ( ret != -EAGAIN )
					continue;

				if ( ret != 0 )
					goto clean;

				if ( dev_flash ) {
					dev_cold_flash = 0;
					continue;
				}

				break;

			}

			if ( usb_dev->flash_device->protocol != FLASH_NOLO ) {
				printf("Only NOLO protocol is supported now\n");
				usb_close_device(usb_dev);
				usb_dev = NULL;
				break;
			}

			if ( nolo_init(usb_dev) < 0 ) {
				printf("Cannot initialize NOLO\n");
				usb_close_device(usb_dev);
				usb_dev = NULL;
				continue;
			}

			usb_dev->detected_device = nolo_get_device(usb_dev);
			if ( ! usb_dev->detected_device )
				printf("Device: (not detected)\n");
			else
				printf("Device: %s\n", device_to_string(usb_dev->detected_device));

			buf[0] = 0;
			nolo_get_hwrev(usb_dev, buf, sizeof(buf));
			printf("HW revision: %s\n", buf[0] ? buf : "(not detected)");

			if ( buf[0] )
				usb_dev->detected_hwrev = strdup(buf);

			buf[0] = 0;
			nolo_get_nolo_ver(usb_dev, buf, sizeof(buf));
			printf("NOLO version: %s\n", buf[0] ? buf : "(not detected)");

			buf[0] = 0;
			nolo_get_kernel_ver(usb_dev, buf, sizeof(buf));
			printf("Kernel version: %s\n", buf[0] ? buf : "(not detected)");

			buf[0] = 0;
			nolo_get_sw_ver(usb_dev, buf, sizeof(buf));
			printf("Software release version: %s\n", buf[0] ? buf : "(not detected)");

			buf[0] = 0;
			nolo_get_content_ver(usb_dev, buf, sizeof(buf));
			printf("Content eMMC version: %s\n", buf[0] ? buf : "(not detected)");

			ret = nolo_get_root_device(usb_dev);
			printf("Root device: ");
			if ( ret == 0 )
				printf("flash");
			else if ( ret == 1 )
				printf("mmc");
			else if ( ret == 2 )
				printf("usb");
			else
				printf("(not detected)");
			printf("\n");

			ret = nolo_get_usb_host_mode(usb_dev);
			printf("USB host mode: ");
			if ( ret == 0 )
				printf("disabled");
			else if ( ret == 1 )
				printf("enabled");
			else
				printf("(not detected)");
			printf("\n");

			ret = nolo_get_rd_mode(usb_dev);
			printf("R&D mode: ");
			if ( ret == 0 )
				printf("disabled");
			else if ( ret == 1 )
				printf("enabled");
			else
				printf("(not detected)");
			printf("\n");

			if ( ret == 1 ) {
				ret = nolo_get_rd_flags(usb_dev, buf, sizeof(buf));
				printf("R&D flags: ");
				if ( ret < 0 )
					printf("(not detected)");
				else
					printf("%s", buf);
				printf("\n");
			}

			/* device identify */
			if ( dev_ident ) {
				usb_close_device(usb_dev);
				usb_dev = NULL;
				break;
			}

			printf("\n");

			/* filter images by device & hwrev */
			if ( usb_dev->detected_device )
				filter_images_by_device(usb_dev->detected_device, &image_first);
			if ( usb_dev->detected_hwrev )
				filter_images_by_hwrev(usb_dev->detected_hwrev, &image_first);

			/* load */
			if ( dev_load ) {
//			if ( image_first )
			}

			/* flash */
			if ( dev_flash) {
//			if ( image_first )
			}

			/* configuration */
			if ( set_rd_flags ) {
				set_rd = 1;
				set_rd_arg = "1";
			}
			if ( set_root )
				nolo_set_root_device(usb_dev, atoi(set_root_arg));
			if ( set_usb )
				nolo_set_usb_host_mode(usb_dev, atoi(set_usb_arg));
			if ( set_rd )
				nolo_set_rd_mode(usb_dev, atoi(set_rd_arg));
			if ( set_rd_flags )
				nolo_set_rd_flags(usb_dev, set_rd_flags_arg);
			if ( set_hw )
				nolo_set_hwrev(usb_dev, set_hw_arg);
			if ( set_nolo )
				nolo_set_nolo_ver(usb_dev, set_nolo_arg);
			if ( set_kernel )
				nolo_set_kernel_ver(usb_dev, set_kernel_arg);
			if ( set_sw )
				nolo_set_sw_ver(usb_dev, set_sw_arg);
			if ( set_emmc )
				nolo_set_content_ver(usb_dev, set_emmc_arg);

			/* boot */
			if ( dev_boot ) {
				nolo_boot_device(usb_dev, dev_boot_arg);
				usb_close_device(usb_dev);
				usb_dev = NULL;
				break;
			}

			/* reboot */
			if ( dev_reboot ) {
				nolo_reboot_device(usb_dev);
				usb_close_device(usb_dev);
				usb_dev = NULL;
				break;
			}

		} while ( dev_cold_flash || dev_flash );

	}


#endif


#ifdef WITH_DEVICE

	/* on device */

	/* device identify */

	/* check */

	/* dump */

	/* flash */

	/* configuration */

	/* reboot */

#endif

	ret = 0;

	/* clean */
clean:

	if ( ! image_fiasco ) {
		image_ptr = image_first;
		while ( image_ptr ) {
			struct image_list * next = image_ptr->next;
			image_list_del(image_ptr);
			image_ptr = next;
		}
	}

	if ( fiasco_in )
		fiasco_free(fiasco_in);

	if ( usb_dev )
		usb_close_device(usb_dev);

	return ret;
}
