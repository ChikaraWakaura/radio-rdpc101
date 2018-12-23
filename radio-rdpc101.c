/*
 *  drivers/media/radio/radio-rdpc101.c
 *
 *  Driver for SUNTAC USB AM/FM radio receiver
 *
 *  derived from radio-si470x.c
 *
 *  Copyright 2009 (c) by Driver Labo. (http://www.drvlabo.jp/wp/archives/72)
 *                        dafmemo (http://dafmemo.blogspot.com/2009/11/linux-usb-radio-peercast.html)
 *                        izumogeiger (https://gist.github.com/izumogeiger/6268289)
 *                        Chikara Wakaura <wakaurac@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */


/* driver definitions */
#define	DEVICE_RDPC101
#define DRIVER_AUTHOR "Driver Labo."
#define DRIVER_NAME "radio-rdpc101"
#define DRIVER_CARD "SUNTAC RDPC-101 USB radio receiver"
#define DRIVER_DESC "USB radio driver for SUNTAC RDPC-101"
#define DRIVER_VERSION "0.0.1"


/* kernel includes */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <linux/mutex.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-device.h>
#include <asm/unaligned.h>


/* USB Device ID List */
static struct usb_device_id rdpc101_usb_driver_id_table[] = {
	/* Silicon Labs USB FM Radio Reference Design */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x10c4, 0x818a, USB_CLASS_HID, 0, 0) },
	/* Terminating entry */
	{ }
};
MODULE_DEVICE_TABLE(usb, rdpc101_usb_driver_id_table);



/**************************************************************************
 * Module Parameters
 **************************************************************************/

#if !defined(DEVICE_RDPC101)
/* Spacing (kHz) */
/* 0: 200 kHz (USA, Australia) */
/* 1: 100 kHz (Europe, Japan) */
/* 2:  50 kHz */
static unsigned short space = 1;
module_param(space, ushort, 0444);
MODULE_PARM_DESC(space, "Spacing: 0=200kHz 1=100kHz *2=50kHz*");
#endif

/* De-emphasis */
/* 0: 75 us (USA) */
/* 1: 50 us (Europe, Australia, Japan) */
static unsigned short de = 1;
module_param(de, ushort, 0444);
MODULE_PARM_DESC(de, "De-emphasis: 0=75us *1=50us*");

/* Radio Nr */
static int radio_nr = -1;
module_param(radio_nr, int, 0444);
MODULE_PARM_DESC(radio_nr, "Radio Nr");

/* USB timeout */
static unsigned int usb_timeout = 500;
module_param(usb_timeout, uint, 0644);
MODULE_PARM_DESC(usb_timeout, "USB timeout (ms): *500*");

/* RDS maximum block errors */
static unsigned short max_rds_errors = 1;
/* 0 means   0  errors requiring correction */
/* 1 means 1-2  errors requiring correction (used by original USBRadio.exe) */
/* 2 means 3-5  errors requiring correction */
/* 3 means   6+ errors or errors in checkword, correction not possible */
module_param(max_rds_errors, ushort, 0644);
MODULE_PARM_DESC(max_rds_errors, "RDS maximum block errors: *1*");

#if !defined(DEVICE_RDPC101)
/* Tune timeout */
static unsigned int tune_timeout = 3000;
module_param(tune_timeout, uint, 0644);
MODULE_PARM_DESC(tune_timeout, "Tune timeout: *3000*");
#endif

/* Seek timeout */
static unsigned int seek_timeout = 5000;
module_param(seek_timeout, uint, 0644);
MODULE_PARM_DESC(seek_timeout, "Seek timeout: *5000*");

#define	FREQ_STEP_FM_KHZ	(100)	/* 100[kHz] */
#define	FREQ_STEP_AM_KHZ	(9)	/*   9[kHz] */

/*
 * FM: 76.0 - 90.0 [MHz]
 * AM: 522 - 1629 [kHz]
 */
#define	FREQ_LOW_FM_KHZ		(76 * 1000)
#define	FREQ_HIGH_FM_KHZ	(90 * 1000)
#define	FREQ_LOW_AM_KHZ		(522)
#define	FREQ_HIGH_AM_KHZ	(1629)


/**************************************************************************
 * Register Definitions
 **************************************************************************/
#define RADIO_REGISTER_SIZE	2	/* 16 register bit width */
#define RADIO_REGISTER_NUM	16	/* DEVICEID   ... RDSD */
#define RDS_REGISTER_NUM	6	/* STATUSRSSI ... RDSD */

#define DEVICEID		0	/* Device ID */
#define DEVICEID_PN		0xf000	/* bits 15..12: Part Number */
#define DEVICEID_MFGID		0x0fff	/* bits 11..00: Manufacturer ID */

#define	SI_CHIPID		1       /* Chip ID */
#define	SI_CHIPID_REV		0xfc00  /* bits 15..10: Chip Version */
#define	SI_CHIPID_DEV		0x0200  /* bits 09..09: Device */
#define	SI_CHIPID_FIRMWARE	0x01ff  /* bits 08..00: Firmware Version */

#define CHIPID			1	/* Chip ID */
#define CHIPID_REV		0xfc00	/* bits 15..10: Chip Version */
#define CHIPID_DEV		0x0200	/* bits 09..09: Device */
#define CHIPID_FIRMWARE		0x01ff	/* bits 08..00: Firmware Version */

#define POWERCFG		2	/* Power Configuration */
#define POWERCFG_DSMUTE		0x8000	/* bits 15..15: Softmute Disable */
#define POWERCFG_DMUTE		0x4000	/* bits 14..14: Mute Disable */
#define POWERCFG_MONO		0x2000	/* bits 13..13: Mono Select */
#define POWERCFG_RDSM		0x0800	/* bits 11..11: RDS Mode (Si4701 only) */
#define POWERCFG_SKMODE		0x0400	/* bits 10..10: Seek Mode */
#define POWERCFG_SEEKUP		0x0200	/* bits 09..09: Seek Direction */
#define POWERCFG_SEEK		0x0100	/* bits 08..08: Seek */
#define POWERCFG_DISABLE	0x0040	/* bits 06..06: Powerup Disable */
#define POWERCFG_ENABLE		0x0001	/* bits 00..00: Powerup Enable */

#define CHANNEL			3	/* Channel */
#define CHANNEL_TUNE		0x8000	/* bits 15..15: Tune */
#define CHANNEL_CHAN		0x03ff	/* bits 09..00: Channel Select */

#define SYSCONFIG1		4	/* System Configuration 1 */
#define SYSCONFIG1_RDSIEN	0x8000	/* bits 15..15: RDS Interrupt Enable (Si4701 only) */
#define SYSCONFIG1_STCIEN	0x4000	/* bits 14..14: Seek/Tune Complete Interrupt Enable */
#define SYSCONFIG1_RDS		0x1000	/* bits 12..12: RDS Enable (Si4701 only) */
#define SYSCONFIG1_DE		0x0800	/* bits 11..11: De-emphasis (0=75us 1=50us) */
#define SYSCONFIG1_AGCD		0x0400	/* bits 10..10: AGC Disable */
#define SYSCONFIG1_BLNDADJ	0x00c0	/* bits 07..06: Stereo/Mono Blend Level Adjustment */
#define SYSCONFIG1_GPIO3	0x0030	/* bits 05..04: General Purpose I/O 3 */
#define SYSCONFIG1_GPIO2	0x000c	/* bits 03..02: General Purpose I/O 2 */
#define SYSCONFIG1_GPIO1	0x0003	/* bits 01..00: General Purpose I/O 1 */

#define SYSCONFIG2		5	/* System Configuration 2 */
#define SYSCONFIG2_SEEKTH	0xff00	/* bits 15..08: RSSI Seek Threshold */
#define SYSCONFIG2_BAND		0x00c0	/* bits 07..06: Band Select */
#define SYSCONFIG2_SPACE	0x0030	/* bits 05..04: Channel Spacing */
#define SYSCONFIG2_VOLUME	0x000f	/* bits 03..00: Volume */

#define SYSCONFIG3		6	/* System Configuration 3 */
#define SYSCONFIG3_SMUTER	0xc000	/* bits 15..14: Softmute Attack/Recover Rate */
#define SYSCONFIG3_SMUTEA	0x3000	/* bits 13..12: Softmute Attenuation */
#define SYSCONFIG3_SKSNR	0x00f0	/* bits 07..04: Seek SNR Threshold */
#define SYSCONFIG3_SKCNT	0x000f	/* bits 03..00: Seek FM Impulse Detection Threshold */

#define TEST1			7	/* Test 1 */
#define TEST1_AHIZEN		0x4000	/* bits 14..14: Audio High-Z Enable */

#define TEST2			8	/* Test 2 */
/* TEST2 only contains reserved bits */

#define BOOTCONFIG		9	/* Boot Configuration */
/* BOOTCONFIG only contains reserved bits */

#define STATUSRSSI		10	/* Status RSSI */
#define STATUSRSSI_RDSR		0x8000	/* bits 15..15: RDS Ready (Si4701 only) */
#define STATUSRSSI_STC		0x4000	/* bits 14..14: Seek/Tune Complete */
#define STATUSRSSI_SF		0x2000	/* bits 13..13: Seek Fail/Band Limit */
#define STATUSRSSI_AFCRL	0x1000	/* bits 12..12: AFC Rail */
#define STATUSRSSI_RDSS		0x0800	/* bits 11..11: RDS Synchronized (Si4701 only) */
#define STATUSRSSI_BLERA	0x0600	/* bits 10..09: RDS Block A Errors (Si4701 only) */
#define STATUSRSSI_ST		0x0100	/* bits 08..08: Stereo Indicator */
#define STATUSRSSI_RSSI		0x00ff	/* bits 07..00: RSSI (Received Signal Strength Indicator) */

#define READCHAN		11	/* Read Channel */
#define READCHAN_BLERB		0xc000	/* bits 15..14: RDS Block D Errors (Si4701 only) */
#define READCHAN_BLERC		0x3000	/* bits 13..12: RDS Block C Errors (Si4701 only) */
#define READCHAN_BLERD		0x0c00	/* bits 11..10: RDS Block B Errors (Si4701 only) */
#define READCHAN_READCHAN	0x03ff	/* bits 09..00: Read Channel */

#define RDSA			12	/* RDSA */
#define RDSA_RDSA		0xffff	/* bits 15..00: RDS Block A Data (Si4701 only) */

#define RDSB			13	/* RDSB */
#define RDSB_RDSB		0xffff	/* bits 15..00: RDS Block B Data (Si4701 only) */

#define RDSC			14	/* RDSC */
#define RDSC_RDSC		0xffff	/* bits 15..00: RDS Block C Data (Si4701 only) */

#define RDSD			15	/* RDSD */
#define RDSD_RDSD		0xffff	/* bits 15..00: RDS Block D Data (Si4701 only) */



/**************************************************************************
 * USB HID Reports
 **************************************************************************/
#define	REGISTER_REPORT_SIZE	(RADIO_REGISTER_SIZE + 1)
#define	REGISTER_REPORT(reg)	((reg) + 1)

#define	ENTIRE_REPORT_SIZE	(RADIO_REGISTER_NUM * RADIO_REGISTER_SIZE + 1)
#define	ENTIRE_REPORT		17

#define RDS_REPORT_SIZE		(RDS_REGISTER_NUM * RADIO_REGISTER_SIZE + 1)
#define RDS_REPORT		18

/*
 * frequency setting
 */
#define	REPORT_ID_SET_FREQ	(0x02)
#define	REPORT_SIZE_SET_FREQ	(3)

/*
 * AM/FM tuner mode switching
 */
#define	REPORT_ID_TUNER_MODE	(0x0A)
#define	REPORT_SIZE_TUNER_MODE	(3)

/* Report 19: LED state */
#define LED_REPORT_SIZE		3
#define LED_REPORT		19

/* Report 20: scratch */
#define SCRATCH_PAGE_SIZE	63
#define SCRATCH_REPORT_SIZE	(SCRATCH_PAGE_SIZE + 1)
#define SCRATCH_REPORT		20

#define MAX_REPORT_SIZE		64


/**************************************************************************
 * Software/Hardware Versions
 **************************************************************************/
#define RADIO_SW_VERSION_NOT_BOOTLOADABLE	6
#define RADIO_SW_VERSION			7
#define RADIO_SW_VERSION_CURRENT		15
#define RADIO_HW_VERSION			1


/**************************************************************************
 * LED State Definitions
 **************************************************************************/
#define LED_COMMAND		0x35

#define NO_CHANGE_LED		0x00
#define ALL_COLOR_LED		0x01	/* streaming state */
#define BLINK_GREEN_LED		0x02	/* connect state */
#define BLINK_RED_LED		0x04
#define BLINK_ORANGE_LED	0x10	/* disconnect state */
#define SOLID_GREEN_LED		0x20	/* tuning/seeking state */
#define SOLID_RED_LED		0x40	/* bootload state */
#define SOLID_ORANGE_LED	0x80

extern const struct v4l2_ctrl_ops si470x_ctrl_ops;


/*
 * rdpc101_device - private data
 */
struct rdpc101_device {
	struct v4l2_device v4l2_dev;
	struct video_device videodev;
	struct v4l2_ctrl_handler hdl;
	int band;

	/* Silabs internal registers (0..15) */
	unsigned short registers[RADIO_REGISTER_NUM];

	/* RDS receive buffer */
	wait_queue_head_t read_queue;
	struct mutex lock;		/* buffer locking */
	unsigned char *buffer;		/* size is always multiple of three */
	unsigned int buf_size;
	unsigned int rd_index;
	unsigned int wr_index;

	struct completion completion;
	bool status_rssi_auto_update;

	/* reference to USB and video device */
	struct usb_device *usbdev;
	struct usb_interface *intf;
	char *usb_buf;

	/* Interrupt endpoint handling */
	char *int_in_buffer;
	struct usb_endpoint_descriptor *int_in_endpoint;
	struct urb *int_in_urb;
	int int_in_running;

	/* scratch page */
	unsigned char software_version;
	unsigned char hardware_version;

	/* for control */
	int  freq_khz;		/* frequency as kHz */
};


/*
 * The frequency is set in units of 62.5 Hz when using V4L2_TUNER_CAP_LOW,
 * 62.5 kHz otherwise.
 * The tuner is able to have a channel spacing of 50, 100 or 200 kHz.
 * tuner->capability is therefore set to V4L2_TUNER_CAP_LOW
 * The FREQ_MUL is then: 1 MHz / 62.5 Hz = 16000
 */
#define FREQ_MHZ_MUL (10000000 / 625)
#define FREQ_KHZ_MUL (10000 / 625)

#define	FREQ_KHZ_TO_V4L(val)	((val) * 16)
#define	FREQ_V4L_TO_KHZ(val)	((val) / 16)


static const struct v4l2_frequency_band bands[] = {
	{
		.type = V4L2_TUNER_RADIO,
		.index = 0,
		.capability = V4L2_TUNER_CAP_LOW
			    | V4L2_TUNER_CAP_STEREO
			    | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   = ( FREQ_LOW_FM_KHZ  / 1000 ) * FREQ_MHZ_MUL,
		.rangehigh  = ( FREQ_HIGH_FM_KHZ / 1000 ) * FREQ_MHZ_MUL,
		.modulation = V4L2_BAND_MODULATION_FM,
	},
	{
		.type = V4L2_TUNER_RADIO,
		.index = 1,
		.capability = V4L2_TUNER_CAP_LOW
			    | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   = FREQ_LOW_AM_KHZ  * FREQ_KHZ_MUL,
		.rangehigh  = FREQ_HIGH_AM_KHZ * FREQ_KHZ_MUL,
		.modulation = V4L2_BAND_MODULATION_AM,
	},
};

/**************************************************************************
 * Generic Functions
 **************************************************************************/

/*
 * rdpc101_get_report - receive a HID report
 */
static int rdpc101_get_report(struct rdpc101_device *radio, void *buf, int size, int iLineNo)
{
	unsigned char *report = buf;
	int retval;

	retval = usb_control_msg(radio->usbdev,
		usb_rcvctrlpipe(radio->usbdev, 0),
		HID_REQ_GET_REPORT,
		USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
		report[0], 2,
		buf, size, usb_timeout);

	if (retval < 0)
		printk(KERN_WARNING DRIVER_NAME
			": rdpc101_get_report: usb_control_msg returned %d call line %d\n",
			retval, iLineNo );
	return retval;
}


/*
 * rdpc101_set_report - send a HID report
 */
static int rdpc101_set_report(struct rdpc101_device *radio, void *buf, int size, int iLineNo)
{
	unsigned char *report = (unsigned char *) buf;
	int retval;

	retval = usb_control_msg(radio->usbdev,
		usb_sndctrlpipe(radio->usbdev, 0),
		HID_REQ_SET_REPORT,
		USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT,
		report[0], 2,
		buf, size, usb_timeout);

	if (retval < 0)
		printk(KERN_WARNING DRIVER_NAME
			": rdpc101_set_report: usb_control_msg returned %d call line %d\n",
			retval, iLineNo );
	return retval;
}


/*
 * rdpc101_get_register - read register
 */
int rdpc101_get_register(struct rdpc101_device *radio, int regnr, int iLineNo)
{
	int retval;

	radio->usb_buf[0] = REGISTER_REPORT(regnr);

	retval = rdpc101_get_report(radio, radio->usb_buf, REGISTER_REPORT_SIZE, iLineNo);

	if (retval >= 0)
		radio->registers[regnr] = get_unaligned_be16(&radio->usb_buf[1]);

	return (retval < 0) ? -EINVAL : 0;
}


/*
 * rdpc101_set_register - write register
 */
static int rdpc101_set_register(struct rdpc101_device *radio, int regnr, int iLineNo)
{
	int retval;

	radio->usb_buf[0] = REGISTER_REPORT(regnr);
	put_unaligned_be16(radio->registers[regnr], &radio->usb_buf[1]);

	retval = rdpc101_set_report(radio, radio->usb_buf, REGISTER_REPORT_SIZE, iLineNo );

	return (retval < 0) ? -EINVAL : 0;
}


/*
 * rdpc101_get_all_registers - read entire registers
 */
static int rdpc101_get_all_registers(struct rdpc101_device *radio)
{
	int retval;
	unsigned char regnr;

	radio->usb_buf[0] = ENTIRE_REPORT;

	retval = rdpc101_get_report(radio, radio->usb_buf, ENTIRE_REPORT_SIZE, __LINE__ );

	if (retval >= 0)
		for (regnr = 0; regnr < RADIO_REGISTER_NUM; regnr++)
			radio->registers[regnr] = get_unaligned_be16(
				&radio->usb_buf[regnr * RADIO_REGISTER_SIZE + 1]);

	return (retval < 0) ? -EINVAL : 0;
}

#if !defined(DEVICE_RDPC101)
/*
 * rdpc101_set_chan - set the channel
 */
static int rdpc101_set_chan(struct rdpc101_device *radio, unsigned short chan)
{
	int retval;
	unsigned long time_left;
	bool timed_out = false;

	/* start tuning */
	radio->registers[CHANNEL] &= ~CHANNEL_CHAN;
	radio->registers[CHANNEL] |= CHANNEL_TUNE | chan;
	retval = rdpc101_set_register(radio, CHANNEL, __LINE__ );
	if (retval < 0)
		goto done;

	/* wait till tune operation has completed */
	reinit_completion(&radio->completion);
	time_left = wait_for_completion_timeout(&radio->completion,
						msecs_to_jiffies(tune_timeout));
	if (time_left == 0)
		timed_out = true;

	if ((radio->registers[STATUSRSSI] & STATUSRSSI_STC) == 0)
		printk(KERN_WARNING DRIVER_NAME
			": tune does not complete\n");
	if (timed_out)
		printk(KERN_WARNING DRIVER_NAME
			": tune timed out after %u ms\n", tune_timeout);

	/* stop tuning */
	radio->registers[CHANNEL] &= ~CHANNEL_TUNE;
	retval = rdpc101_set_register(radio, CHANNEL, __LINE__ );

done:
	return retval;
}
#endif


/*
 * rdpc101_start - switch on radio
 */
int rdpc101_start(struct rdpc101_device *radio)
{
	int retval = 0;

#if !defined(DEVICE_RDPC101)
	/* SUNTAC RDPC-101 SYSCONFIG2 set to usb_control_msg returned -110 */
	/* powercfg */
	radio->registers[POWERCFG] =
		POWERCFG_DMUTE | POWERCFG_ENABLE | POWERCFG_RDSM;
	retval = rdpc101_set_register(radio, POWERCFG, __LINE__ );
	if (retval < 0)	{
		goto done;
	}
#endif

	/* sysconfig 1 */
	radio->registers[SYSCONFIG1] =
		(de << 11) & SYSCONFIG1_DE;		/* DE*/
	retval = rdpc101_set_register(radio, SYSCONFIG1, __LINE__ );
	if (retval < 0) {
		goto done;
	}

#if !defined(DEVICE_RDPC101)
	/* SUNTAC RDPC-101 SYSCONFIG2 set to usb_control_msg returned -110 */
	/* sysconfig 2 */
	radio->registers[SYSCONFIG2] =
		(0x1f  << 8) |					/* SEEKTH */
		((radio->band << 6) & SYSCONFIG2_BAND) |	/* BAND */
		((space << 4) & SYSCONFIG2_SPACE) |		/* SPACE */
		15;						/* VOLUME (max) */
	retval = rdpc101_set_register(radio, SYSCONFIG2, __LINE__ );
	if (retval < 0) {
		goto done;
	}
#endif

#if !defined(DEVICE_RDPC101)
	/* SUNTAC RDPC-101 CHANNEL set to 
	   tune does not complete
	   tune timed out after 3000 ms */
	/* reset last channel */
	retval = rdpc101_set_chan(radio,
				  radio->registers[CHANNEL] & CHANNEL_CHAN);
	if (retval < 0 ) {
		printk(KERN_INFO DRIVER_NAME ": failed. [0x%04hX]\n",
			radio->registers[CHANNEL] & CHANNEL_CHAN );
	}
#endif

done:
	return retval;
}


/*
 * rdpc101_stop - switch off radio
 */
int rdpc101_stop(struct rdpc101_device *radio)
{
	int retval;

	/* sysconfig 1 */
	radio->registers[SYSCONFIG1] &= ~SYSCONFIG1_RDS;
	retval = rdpc101_set_register(radio, SYSCONFIG1, __LINE__ );
	if (retval < 0)
		goto done;

	/* powercfg */
	radio->registers[POWERCFG] &= ~POWERCFG_DMUTE;
	/* POWERCFG_ENABLE has to automatically go low */
	radio->registers[POWERCFG] |= POWERCFG_ENABLE | POWERCFG_DISABLE;
	retval = rdpc101_set_register(radio, POWERCFG, __LINE__ );

done:
	return retval;
}


/*
 * si470x_set_band - set the band
 */
static int rdpc101_set_band(struct rdpc101_device *radio, int band)
{
	int mode, retval;

	if ( band >= 0 && band < ARRAY_SIZE( bands ) )
		radio->band = band;

	mode = bands[radio->band].modulation;

	radio->usb_buf[0] = REPORT_ID_TUNER_MODE;
	radio->usb_buf[1] = (mode == V4L2_BAND_MODULATION_AM) ? 0x80 : 0x02;
	radio->usb_buf[2] = 0x02;

	retval = rdpc101_set_report( radio, radio->usb_buf, REPORT_SIZE_TUNER_MODE, __LINE__ );

	return (retval < 0) ? -EINVAL : 0;
}


/*
 * rdpc101_get_freq - get the frequency
 */
static int rdpc101_get_freq(struct rdpc101_device *radio, unsigned int *freq)
{
	*freq = FREQ_KHZ_TO_V4L( radio->freq_khz );

	return 0;
}

/*
 * rdpc101_set_freq - set the frequency
 */
static int rdpc101_set_freq(struct rdpc101_device *radio, unsigned int freq)
{
	int  freq_khz;
	int  freq_step;
	int  freq_low;
	int  freq_high;
	int  temp;
	int  capa;
	int  mode;
	int  retval;

	freq_khz = FREQ_V4L_TO_KHZ( freq );
	capa = bands[radio->band].capability;
	mode = bands[radio->band].modulation;

	if ( capa & V4L2_TUNER_CAP_LOW )
		freq_khz = FREQ_V4L_TO_KHZ( freq );
	else if ( capa & V4L2_TUNER_CAP_1HZ )
		freq_khz = freq / 1000; 

	if( mode == V4L2_BAND_MODULATION_AM ) {
		freq_step = FREQ_STEP_AM_KHZ;
		freq_low  = FREQ_LOW_AM_KHZ;
		freq_high = FREQ_HIGH_AM_KHZ;
	} else {
		freq_step = FREQ_STEP_FM_KHZ;
		freq_low  = FREQ_LOW_FM_KHZ;
		freq_high = FREQ_HIGH_FM_KHZ;
	}

	if( (freq_khz < freq_low) || (freq_high < freq_khz) ){
		return -EINVAL;
	}
	/* round frequency by step value */
	temp = freq_khz % freq_step;
	freq_khz -= temp;

	radio->freq_khz = freq_khz;

	radio->usb_buf[0] = REPORT_ID_SET_FREQ;
	if( mode == V4L2_BAND_MODULATION_FM ){
		freq_khz /= 10;
	}
	radio->usb_buf[1] = (freq_khz >> 8) & 0xFF;
	radio->usb_buf[2] = freq_khz & 0xFF;

	if ( mode == V4L2_BAND_MODULATION_FM )
		printk(KERN_INFO DRIVER_NAME ": set freq FM %d.%02d MHz\n", freq_khz / 100, freq_khz % 100 );
	else
		printk(KERN_INFO DRIVER_NAME ": set freq AM %d kHz\n", freq_khz );

	retval = rdpc101_set_report( radio, radio->usb_buf, REPORT_SIZE_SET_FREQ, __LINE__ );

	return (retval < 0) ? -EINVAL : 0;
}

/*
 * rdpc101_set_seek - set seek
 */
static int rdpc101_set_seek(struct rdpc101_device *radio,
			    const struct v4l2_hw_freq_seek *seek)
{
	int band, retval;
	unsigned int freq;
	bool timed_out = false;
	unsigned long time_left;

	/* set band */
	if (seek->rangelow || seek->rangehigh) {
		for (band = 0; band < ARRAY_SIZE(bands); band++) {
			if (bands[band].rangelow  == seek->rangelow &&
			    bands[band].rangehigh == seek->rangehigh)
				break;
		}
		if (band == ARRAY_SIZE(bands))
			return -EINVAL; /* No matching band found */
	} else
		band = 0; /* If nothing is specified seek 76 - 108 Mhz */

	if (radio->band != band) {
		retval = rdpc101_get_freq(radio, &freq);
		if (retval)
			return retval;
		retval = rdpc101_set_band(radio, band);
		if (retval)
			return retval;
		retval = rdpc101_set_freq(radio, freq);
		if (retval)
			return retval;
	}

	/* start seeking */
	radio->registers[POWERCFG] |= POWERCFG_SEEK;
	if (seek->wrap_around)
		radio->registers[POWERCFG] &= ~POWERCFG_SKMODE;
	else
		radio->registers[POWERCFG] |= POWERCFG_SKMODE;
	if (seek->seek_upward)
		radio->registers[POWERCFG] |= POWERCFG_SEEKUP;
	else
		radio->registers[POWERCFG] &= ~POWERCFG_SEEKUP;
	retval = rdpc101_set_register(radio, POWERCFG, __LINE__);
	if (retval < 0)
		return retval;

	/* wait till tune operation has completed */
	reinit_completion(&radio->completion);
	time_left = wait_for_completion_timeout(&radio->completion,
						msecs_to_jiffies(seek_timeout));
	if (time_left == 0)
		timed_out = true;

	if ((radio->registers[STATUSRSSI] & STATUSRSSI_STC) == 0)
		printk(KERN_WARNING DRIVER_NAME ": seek does not complete\n");
	if (radio->registers[STATUSRSSI] & STATUSRSSI_SF)
		printk(KERN_WARNING DRIVER_NAME
			": seek failed / band limit reached\n");

	/* stop seeking */
	radio->registers[POWERCFG] &= ~POWERCFG_SEEK;
	retval = rdpc101_set_register(radio, POWERCFG, __LINE__);

	/* try again, if timed out */
	if (retval == 0 && timed_out)
		return -ENODATA;
	return retval;
}


/*
 * rdpc101_set_led_state - sets the led state
 */
static int rdpc101_set_led_state(struct rdpc101_device *radio,
		unsigned char led_state)
{
	int retval;

	radio->usb_buf[0] = LED_REPORT;
	radio->usb_buf[1] = LED_COMMAND;
	radio->usb_buf[2] = led_state;

	retval = rdpc101_set_report(radio, radio->usb_buf, LED_REPORT_SIZE, __LINE__ );

	return (retval < 0) ? -EINVAL : 0;
}


/*
 * rdpc101_get_scratch_versions - gets the scratch page and version infos
 */
static int rdpc101_get_scratch_page_versions(struct rdpc101_device *radio)
{
	int retval;

	radio->usb_buf[0] = SCRATCH_REPORT;

	retval = rdpc101_get_report(radio, radio->usb_buf, SCRATCH_REPORT_SIZE, __LINE__ );
	if (retval >= 0) {
		radio->software_version = radio->usb_buf[1];
		radio->hardware_version = radio->usb_buf[2];
	}

	return (retval < 0) ? -EINVAL : 0;
}

/*
 * rdpc101_int_in_callback - rds callback and processing function
 *
 * TODO: do we need to use mutex locks in some sections?
 */
static void rdpc101_int_in_callback(struct urb *urb)
{
	struct rdpc101_device *radio = urb->context;
	int retval;
	unsigned char regnr;
	unsigned char blocknum;
	unsigned short bler; /* rds block errors */
	unsigned short rds;
	unsigned char tmpbuf[3];

	if (urb->status) {
		if (urb->status == -ENOENT
		 || urb->status == -ECONNRESET
		 || urb->status == -ESHUTDOWN
		 || urb->status == -EPIPE) {
			return;
		} else {
			printk(KERN_WARNING DRIVER_NAME
			 ": non-zero urb status (%d)\n", urb->status);
			goto resubmit; /* Maybe we can recover. */
		}
	}

	/* Sometimes the device returns len 0 packets */
	if (urb->actual_length != RDS_REPORT_SIZE)
		goto resubmit;

	radio->registers[STATUSRSSI] =
		get_unaligned_be16(&radio->int_in_buffer[1]);

	if (radio->registers[STATUSRSSI] & STATUSRSSI_STC)
		complete(&radio->completion);

	if ((radio->registers[SYSCONFIG1] & SYSCONFIG1_RDS)) {
		/* Update RDS registers with URB data */
		for (regnr = 1; regnr < RDS_REGISTER_NUM; regnr++)
			radio->registers[STATUSRSSI + regnr] =
			    get_unaligned_be16(&radio->int_in_buffer[
				regnr * RADIO_REGISTER_SIZE + 1]);
		/* get rds blocks */
		if ((radio->registers[STATUSRSSI] & STATUSRSSI_RDSR) == 0) {
			/* No RDS group ready, better luck next time */
			goto resubmit;
		}
		if ((radio->registers[STATUSRSSI] & STATUSRSSI_RDSS) == 0) {
			/* RDS decoder not synchronized */
			goto resubmit;
		}
		for (blocknum = 0; blocknum < 4; blocknum++) {
			switch (blocknum) {
			default:
				bler = (radio->registers[STATUSRSSI] &
						STATUSRSSI_BLERA) >> 9;
				rds = radio->registers[RDSA];
				break;
			case 1:
				bler = (radio->registers[READCHAN] &
						READCHAN_BLERB) >> 14;
				rds = radio->registers[RDSB];
				break;
			case 2:
				bler = (radio->registers[READCHAN] &
						READCHAN_BLERC) >> 12;
				rds = radio->registers[RDSC];
				break;
			case 3:
				bler = (radio->registers[READCHAN] &
						READCHAN_BLERD) >> 10;
				rds = radio->registers[RDSD];
				break;
			}

			/* Fill the V4L2 RDS buffer */
			put_unaligned_le16(rds, &tmpbuf);
			tmpbuf[2] = blocknum;		/* offset name */
			tmpbuf[2] |= blocknum << 3;	/* received offset */
			if (bler > max_rds_errors)
				tmpbuf[2] |= 0x80;	/* uncorrectable errors */
			else if (bler > 0)
				tmpbuf[2] |= 0x40;	/* corrected error(s) */

			/* copy RDS block to internal buffer */
			memcpy(&radio->buffer[radio->wr_index], &tmpbuf, 3);
			radio->wr_index += 3;

			/* wrap write pointer */
			if (radio->wr_index >= radio->buf_size)
				radio->wr_index = 0;

			/* check for overflow */
			if (radio->wr_index == radio->rd_index) {
				/* increment and wrap read pointer */
				radio->rd_index += 3;
				if (radio->rd_index >= radio->buf_size)
					radio->rd_index = 0;
			}
		}
		if (radio->wr_index != radio->rd_index)
			wake_up_interruptible(&radio->read_queue);
	}

resubmit:
	/* Resubmit if we're still running. */
	if (radio->int_in_running && radio->usbdev) {
		retval = usb_submit_urb(radio->int_in_urb, GFP_ATOMIC);
		if (retval) {
			printk(KERN_WARNING DRIVER_NAME
			       ": resubmitting urb failed (%d)", retval);
			radio->int_in_running = 0;
		}
	}
	radio->status_rssi_auto_update = radio->int_in_running;
}


/**************************************************************************
 * File Operations Interface
 **************************************************************************/

/*
 * rdpc101_fops_open - file open
 */
static int rdpc101_fops_open(struct file *file)
{
	return v4l2_fh_open(file);
}


/*
 * rdpc101_fops_release - file release
 */
static int rdpc101_fops_release(struct file *file)
{
	return v4l2_fh_release(file);
}


/*
 * rdpc101_fops - file operations interface
 */
static const struct v4l2_file_operations rdpc101_fops = {
	.owner		= THIS_MODULE,
	.read		= NULL,
	.poll		= NULL,
	.unlocked_ioctl	= video_ioctl2,
	.open		= rdpc101_fops_open,
	.release	= rdpc101_fops_release,
};


static int rdpc101_start_usb(struct rdpc101_device *radio)
{
	int retval;

	/* initialize interrupt urb */
	usb_fill_int_urb(radio->int_in_urb, radio->usbdev,
			usb_rcvintpipe(radio->usbdev,
			radio->int_in_endpoint->bEndpointAddress),
			radio->int_in_buffer,
			le16_to_cpu(radio->int_in_endpoint->wMaxPacketSize),
			rdpc101_int_in_callback,
			radio,
			radio->int_in_endpoint->bInterval);

	radio->int_in_running = 1;
	mb();

	retval = usb_submit_urb(radio->int_in_urb, GFP_KERNEL);
	if (retval) {
		printk(KERN_WARNING DRIVER_NAME
			": submitting int urb failed (%d)\n", retval);
		radio->int_in_running = 0;
	}
	radio->status_rssi_auto_update = radio->int_in_running;

	/* start radio */
	retval = rdpc101_start(radio);
	if (retval < 0)
		return retval;

	v4l2_ctrl_handler_setup(&radio->hdl);

	return retval;
}


static void rdpc101_usb_release(struct v4l2_device *v4l2_dev)
{
	struct rdpc101_device *radio =
		container_of(v4l2_dev, struct rdpc101_device, v4l2_dev);

	v4l2_ctrl_handler_free(&radio->hdl);
	v4l2_device_unregister(&radio->v4l2_dev);
	kfree(radio->usb_buf);
	kfree(radio);
}


/**************************************************************************
 * Video4Linux Interface
 **************************************************************************/

/*
 * rdpc101_vidioc_querycap - query device capabilities
 */
static int rdpc101_vidioc_querycap(struct file *file, void *priv,
		struct v4l2_capability *capability)
{
	struct rdpc101_device *radio = video_drvdata(file);

	strlcpy(capability->driver, DRIVER_NAME, sizeof(capability->driver));
	strlcpy(capability->card, DRIVER_CARD, sizeof(capability->card));
	usb_make_path(radio->usbdev, capability->bus_info,
			sizeof(capability->bus_info));
	capability->device_caps = V4L2_CAP_TUNER | V4L2_CAP_RADIO;
	capability->capabilities = capability->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}


/*
 * rdpc101_vidioc_g_tuner - get tuner attributes
 */
static int rdpc101_vidioc_g_tuner(struct file *file, void *priv,
		struct v4l2_tuner *tuner)
{
	struct rdpc101_device *radio = video_drvdata(file);
	int retval = 0;

	if (tuner->index != 0) {
		return -EINVAL;
	}

	if (!radio->status_rssi_auto_update) {
		retval = rdpc101_get_register(radio, STATUSRSSI, __LINE__);
		if (retval < 0)
			return retval;
	}

	/* driver constants */
	strcpy(tuner->name, "FM");
	if ( bands[radio->band].modulation == V4L2_BAND_MODULATION_AM )
		strcpy(tuner->name, "AM");
	tuner->type = V4L2_TUNER_RADIO;
	tuner->capability = bands[radio->band].capability;

	/* range limits */
	tuner->rangelow = bands[radio->band].rangelow;
	tuner->rangehigh = bands[radio->band].rangehigh;

	/* stereo indicator == stereo (instead of mono) */
	if ((radio->registers[STATUSRSSI] & STATUSRSSI_ST) == 0)
		tuner->rxsubchans = V4L2_TUNER_SUB_MONO;
	else
		tuner->rxsubchans = V4L2_TUNER_SUB_STEREO;

	/* mono/stereo selector */
	if ((radio->registers[POWERCFG] & POWERCFG_MONO) == 0)
		tuner->audmode = V4L2_TUNER_MODE_STEREO;
	else
		tuner->audmode = V4L2_TUNER_MODE_MONO;

	/* min is worst, max is best; signal:0..0xffff; rssi: 0..0xff */
	/* measured in units of dbμV in 1 db increments (max at ~75 db microV) */
	tuner->signal = (radio->registers[STATUSRSSI] & STATUSRSSI_RSSI);
	/* the ideal factor is 0xffff/75 = 873,8 */
	tuner->signal = (tuner->signal * 873) + (8 * tuner->signal / 10);
	if (tuner->signal > 0xffff)
		tuner->signal = 0xffff;

	/* automatic frequency control: -1: freq to low, 1 freq to high */
	/* AFCRL does only indicate that freq. differs, not if too low/high */
	tuner->afc = (radio->registers[STATUSRSSI] & STATUSRSSI_AFCRL) ? 1 : 0;

	return retval;
}


/*
 * rdpc101_vidioc_s_tuner - set tuner attributes
 */
static int rdpc101_vidioc_s_tuner(struct file *file, void *priv,
		const struct v4l2_tuner *tuner)
{
#if !defined(DEVICE_RDPC101)
	/* SUNTAC RDPC-101 POWERCFG set to usb_control_msg returned -110 */

	struct rdpc101_device *radio = video_drvdata(file);

	if (tuner->index != 0)
		return -EINVAL;

	/* mono/stereo selector */
	switch (tuner->audmode) {
	case V4L2_TUNER_MODE_MONO:
		radio->registers[POWERCFG] |= POWERCFG_MONO;  /* force mono */
		break;
	case V4L2_TUNER_MODE_STEREO:
	default:
		radio->registers[POWERCFG] &= ~POWERCFG_MONO; /* try stereo */
		break;
	}

	return rdpc101_set_register(radio, POWERCFG, __LINE__ );
#else
	return 0;
#endif
}


/*
 * rdpc101_vidioc_g_frequency - get tuner or modulator radio frequency
 */
static int rdpc101_vidioc_g_frequency(struct file *file, void *priv,
		struct v4l2_frequency *freq)
{
	struct rdpc101_device *radio = video_drvdata(file);

	if (freq->tuner != 0)
		return -EINVAL;

	freq->type = V4L2_TUNER_RADIO;
	return rdpc101_get_freq(radio, &freq->frequency);
}


/*
 * rdpc101_vidioc_s_frequency - set tuner or modulator radio frequency
 */
static int rdpc101_vidioc_s_frequency(struct file *file, void *priv,
		const struct v4l2_frequency *freq)
{
	struct rdpc101_device *radio = video_drvdata(file);
	int retval, i, match_band;

	if (freq->tuner != 0)
		return -EINVAL;

	for ( match_band = 0, i = 0; i < ARRAY_SIZE( bands ); i++ ) {
		if ( freq->frequency >= bands[i].rangelow
		  && freq->frequency < bands[i].rangehigh ) {
			match_band = i;
			break;
		}
	}

	retval = rdpc101_set_band(radio, match_band);
	if (retval < 0)
		return retval;

	return rdpc101_set_freq(radio, freq->frequency);
}

/*
 * rdpc101_vidioc_enum_freq_bands - enumerate supported bands
 */
static int rdpc101_vidioc_enum_freq_bands(struct file *file, void *priv,
					  struct v4l2_frequency_band *band)
{
	if (band->tuner != 0)
		return -EINVAL;
	if (band->index >= ARRAY_SIZE(bands))
		return -EINVAL;
	*band = bands[band->index];
	return 0;
}


static int rdpc101_s_ctrl(struct v4l2_ctrl *ctrl)
{
#if !defined(DEVICE_RDPC101)
	struct rdpc101_device *radio =
		container_of(ctrl->handler, struct rdpc101_device, hdl);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_VOLUME:
		/* SUNTAC RDPC-101 SYSCONFIG2 set to usb_control_msg returned -110 */
		radio->registers[SYSCONFIG2] &= ~SYSCONFIG2_VOLUME;
		radio->registers[SYSCONFIG2] |= ctrl->val;
		return rdpc101_set_register(radio, SYSCONFIG2, __LINE__ );

	case V4L2_CID_AUDIO_MUTE:
		/* SUNTAC RDPC-101 POWERCFG set to usb_control_msg returned -110 */
		if (ctrl->val)
			radio->registers[POWERCFG] &= ~POWERCFG_DMUTE;
		else
			radio->registers[POWERCFG] |= POWERCFG_DMUTE;
		return rdpc101_set_register(radio, POWERCFG, __LINE__ );
	default:
		return -EINVAL;
	}
#else
	return 0;
#endif
}


/*
 * rdpc101_vidioc_s_hw_freq_seek - set hardware frequency seek
 */
static int rdpc101_vidioc_s_hw_freq_seek(struct file *file, void *priv,
		const struct v4l2_hw_freq_seek *seek)
{
	struct rdpc101_device *radio = video_drvdata(file);

	if (seek->tuner != 0)
		return -EINVAL;

	if (file->f_flags & O_NONBLOCK)
		return -EWOULDBLOCK;

	return rdpc101_set_seek(radio, seek);
}

static const struct v4l2_ctrl_ops rdpc101_ctrl_ops = {
        .s_ctrl = rdpc101_s_ctrl,
};

/*
 * rdpc101_ioctl_ops - video device ioctl operations
 */
static const struct v4l2_ioctl_ops rdpc101_ioctl_ops = {
	.vidioc_querycap		= rdpc101_vidioc_querycap,
	.vidioc_g_tuner			= rdpc101_vidioc_g_tuner,
	.vidioc_s_tuner			= rdpc101_vidioc_s_tuner,
	.vidioc_g_frequency		= rdpc101_vidioc_g_frequency,
	.vidioc_s_frequency		= rdpc101_vidioc_s_frequency,
	.vidioc_s_hw_freq_seek		= rdpc101_vidioc_s_hw_freq_seek,
	.vidioc_enum_freq_bands		= rdpc101_vidioc_enum_freq_bands,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/*
 * rdpc101_viddev_template - video device interface
 */
static struct video_device rdpc101_viddev_template = {
	.fops			= &rdpc101_fops,
	.name			= DRIVER_NAME,
	.release		= video_device_release,
	.ioctl_ops		= &rdpc101_ioctl_ops,
};



/**************************************************************************
 * USB Interface
 **************************************************************************/

/*
 * rdpc101_usb_driver_probe - probe for the device
 */
static int rdpc101_usb_driver_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct rdpc101_device *radio;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int i, int_end_size, retval = 0;

	/* private data allocation and initialization */
	radio = kzalloc(sizeof(struct rdpc101_device), GFP_KERNEL);
	if (!radio) {
		retval = -ENOMEM;
		goto err_initial;
	}
	radio->usb_buf = kmalloc(MAX_REPORT_SIZE, GFP_KERNEL);
	if (radio->usb_buf == NULL) {
		retval = -ENOMEM;
		goto err_radio;
	}
	radio->usbdev = interface_to_usbdev(intf);
	radio->intf = intf;
	radio->band = 0;		/* Default to FM : 76.0 - 90.0 MHz */
	mutex_init(&radio->lock);
	init_completion(&radio->completion);

	iface_desc = intf->cur_altsetting;

	/* Set up interrupt endpoint information. */
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		if (usb_endpoint_is_int_in(endpoint))
			radio->int_in_endpoint = endpoint;
	}
	if (!radio->int_in_endpoint) {
		printk(KERN_WARNING DRIVER_NAME ": could not find interrupt in endpoint\n");
		retval = -EIO;
		goto err_usbbuf;
	}

	int_end_size = le16_to_cpu(radio->int_in_endpoint->wMaxPacketSize);

	radio->int_in_buffer = kmalloc(int_end_size, GFP_KERNEL);
	if (!radio->int_in_buffer) {
		printk(KERN_WARNING DRIVER_NAME ": could not allocate int_in_buffer");
		retval = -ENOMEM;
		goto err_usbbuf;
	}

	radio->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!radio->int_in_urb) {
		retval = -ENOMEM;
		goto err_intbuffer;
	}

	radio->v4l2_dev.release = rdpc101_usb_release;

	retval = v4l2_device_register(&intf->dev, &radio->v4l2_dev);
	if (retval < 0) {
		printk(KERN_WARNING DRIVER_NAME ": couldn't register v4l2_device\n");
		goto err_urb;
	}

	v4l2_ctrl_handler_init(&radio->hdl, 2);
	v4l2_ctrl_new_std(&radio->hdl, &rdpc101_ctrl_ops,
			  V4L2_CID_AUDIO_MUTE, 0, 1, 1, 1);
	v4l2_ctrl_new_std(&radio->hdl, &rdpc101_ctrl_ops,
			  V4L2_CID_AUDIO_VOLUME, 0, 15, 1, 15);
	if (radio->hdl.error) {
		retval = radio->hdl.error;
		printk(KERN_WARNING DRIVER_NAME ": couldn't register control\n");
		goto err_dev;
	}

	radio->videodev = rdpc101_viddev_template;
	radio->videodev.ctrl_handler = &radio->hdl;
	radio->videodev.lock = &radio->lock;
	radio->videodev.v4l2_dev = &radio->v4l2_dev;
	radio->videodev.release = video_device_release_empty;
	video_set_drvdata(&radio->videodev, radio);

	radio->freq_khz = 0;

	printk(KERN_INFO DRIVER_NAME ": %s, Version %s\n", DRIVER_DESC, DRIVER_VERSION );

	/* get device and chip versions */
	if (rdpc101_get_all_registers(radio) < 0) {
		retval = -EIO;
		goto err_ctrl;
	}

	printk(KERN_INFO DRIVER_NAME ": DeviceID=0x%4.4hX ChipID=0x%4.4hX\n",
				     radio->registers[DEVICEID], radio->registers[SI_CHIPID]);
	printk(KERN_INFO DRIVER_NAME ": device has firmware version %hu.\n",
				     radio->registers[SI_CHIPID] & SI_CHIPID_FIRMWARE);

	/* get software and hardware versions */
	if (rdpc101_get_scratch_page_versions(radio) < 0) {
		retval = -EIO;
		goto err_ctrl;
	}
	printk(KERN_INFO DRIVER_NAME ": software version %u, hardware version %u\n",
					radio->software_version, radio->hardware_version);

	/* set led to connect state */
	rdpc101_set_led_state(radio, BLINK_GREEN_LED);

	/* start radio */
	retval = rdpc101_start_usb(radio);
	if (retval < 0)
                goto err_all;
		
	/* set initial frequency */
	rdpc101_set_band( radio, 0 );				/* 0:FM / 1:AM */
	rdpc101_set_freq( radio, 80 * FREQ_MHZ_MUL );		/* set to 80.0[MHz] (TOKYO FM) */

	/* register video device */
	retval = video_register_device(&radio->videodev, VFL_TYPE_RADIO, radio_nr);
	if (retval) {
		printk(KERN_WARNING DRIVER_NAME
				": Could not register video device\n");
		goto err_all;
	}
	usb_set_intfdata(intf, radio);

	return 0;
err_all:
	kfree(radio->buffer);
err_ctrl:
	v4l2_ctrl_handler_free(&radio->hdl);
err_dev:
	v4l2_device_unregister(&radio->v4l2_dev);
err_urb:
	usb_free_urb(radio->int_in_urb);
err_intbuffer:
	kfree(radio->int_in_buffer);
err_usbbuf:
	kfree(radio->usb_buf);
err_radio:
	kfree(radio);
err_initial:
	return retval;
}


/*
 * rdpc101_usb_driver_suspend - suspend the device
 */
static int rdpc101_usb_driver_suspend(struct usb_interface *intf,
		pm_message_t message)
{
	struct rdpc101_device *radio = usb_get_intfdata(intf);

	printk(KERN_INFO DRIVER_NAME ": suspending now...\n");

	/* shutdown interrupt handler */
	if (radio->int_in_running) {
		radio->int_in_running = 0;
		if (radio->int_in_urb)
			usb_kill_urb(radio->int_in_urb);
	}

	/* cancel read processes */
	wake_up_interruptible(&radio->read_queue);

	/* stop radio */
	rdpc101_stop(radio);

	return 0;
}


/*
 * rdpc101_usb_driver_resume - resume the device
 */
static int rdpc101_usb_driver_resume(struct usb_interface *intf)
{
	struct rdpc101_device *radio = usb_get_intfdata(intf);
	int ret;

	printk(KERN_INFO DRIVER_NAME ": resuming now...\n");

	/* start radio */
	ret = rdpc101_start_usb(radio);
	if (ret == 0)
		 v4l2_ctrl_handler_setup(&radio->hdl);

	return ret;
}


/*
 * rdpc101_usb_driver_disconnect - disconnect the device
 */
static void rdpc101_usb_driver_disconnect(struct usb_interface *intf)
{
	struct rdpc101_device *radio = usb_get_intfdata(intf);

	mutex_lock(&radio->lock);
	v4l2_device_disconnect(&radio->v4l2_dev);
	video_unregister_device(&radio->videodev);
	usb_set_intfdata(intf, NULL);
	mutex_unlock(&radio->lock);
	v4l2_device_put(&radio->v4l2_dev);
}


/*
 * rdpc101_usb_driver - usb driver interface
 */
static struct usb_driver rdpc101_usb_driver = {
	.name			= DRIVER_NAME,
	.probe			= rdpc101_usb_driver_probe,
	.disconnect		= rdpc101_usb_driver_disconnect,
	.suspend		= rdpc101_usb_driver_suspend,
	.resume			= rdpc101_usb_driver_resume,
	.reset_resume		= rdpc101_usb_driver_resume,
	.id_table		= rdpc101_usb_driver_id_table,
};

module_usb_driver(rdpc101_usb_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
