#include "pch.h"
#include "cis_core.h"
#include "cis_img.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#define EP_DATA_IN  0x82
#define EP_ISO_IN   0x86
#define npack 128
#define BUFF_SIZE 1288//*npack

uint8_t cis_start_fg = 0;
static volatile sig_atomic_t do_exit = 0;
static uint8_t buf[npack][BUFF_SIZE];
int done = 0, ncc = 0, rc;
static struct libusb_device_handle* devh = NULL;
unsigned char usbBuff[npack];
static int pkt_num[npack];
unsigned long num_xfer = 0;
extern uint8_t* pixels;


libusb_device_handle* LIBUSB_CALL libusb_open_device_with_vid_pid_num(
	libusb_context* ctx, uint16_t vendor_id, uint16_t product_id, int cis_num)
{
	struct libusb_device** devs;
	struct libusb_device* found = NULL;
	struct libusb_device* dev;
	struct libusb_device_handle* dev_handle = NULL;
	size_t i = 0;
	int r;

	if (libusb_get_device_list(ctx, &devs) < 0)
		return NULL;

	int num = 1;
	while ((dev = devs[i++]) != NULL) {
		struct libusb_device_descriptor desc;
		r = libusb_get_device_descriptor(dev, &desc);
		if (r < 0)
			goto out;
		if (desc.idVendor == vendor_id && desc.idProduct == product_id) {
			if (cis_num == num) {
				found = dev;
				break;
			}
			else {
				num++;
			}
		}
	}

	if (found) {
		r = libusb_open(found, &dev_handle);
		if (r < 0)
			dev_handle = NULL;
	}

out:
	libusb_free_device_list(devs, 1);
	return dev_handle;
}

int cis_usb_init() {

	rc = libusb_init(NULL);
	if (rc < 0) {
		fprintf(stderr, "Error initializing libusb: %s\n", libusb_error_name(rc));
		return 0;
	}

	devh = libusb_open_device_with_vid_pid_num(NULL, 0x04b4, 0x1003, 1);
	if (!devh) {
		fprintf(stderr, "Error finding USB device\n");
		libusb_exit(NULL);
		return 0;
	}

	rc = libusb_claim_interface(devh, 0);
	if (rc < 0) {
		fprintf(stderr, "Error claiming interface: %s\n", libusb_error_name(rc));
		libusb_close(devh);
		libusb_exit(NULL);
		return 0;
	}

	return 1;

}

void cis_usb_deinit(void) {
	if (devh) {
		libusb_release_interface(devh, 0);
		libusb_close(devh);
		devh = NULL;
	}

	libusb_exit(NULL);
}

#include <mutex>
std::mutex mutex_interface;
void LIBUSB_CALL cb_xfr(struct libusb_transfer* xfr)
{
	if (xfr->status != LIBUSB_TRANSFER_COMPLETED) {
		libusb_free_transfer(xfr);
		ncc++;
	}

	else {
		if (cis_start_fg == 1) {
			mutex_interface.lock();
			if (xfr->dev_handle == devh)
				set2buf(xfr->buffer, xfr->actual_length);

			mutex_interface.unlock();
		}
		num_xfer++;
	}
	if (!do_exit) {
		if (libusb_submit_transfer(xfr) < 0) {
			fprintf(stderr, "error re-submitting URB\n");
			libusb_free_transfer(xfr);
		}
	}
	else {
		libusb_cancel_transfer(xfr);
	}

}

void cis_usb_config()
{
	for (int i = 0; i < npack; i++) {
		pkt_num[i] = i;
		uint8_t ep = EP_DATA_IN;
		static struct libusb_transfer* xfr;

		xfr = libusb_alloc_transfer(0);
		libusb_fill_bulk_transfer(xfr, devh, ep, buf[i], sizeof(buf[i]), cb_xfr, &pkt_num[i], 0);

		libusb_submit_transfer(xfr);
	}

	while (!done) {
		rc = libusb_handle_events(NULL);
		if (rc != LIBUSB_SUCCESS)
			break;
	}
}
void cis_usb_start()
{
	cis_start_fg = 1;

	if (enableISP == 1)
	{
		cis_i2c_write(I2C_ADDR, 0x200, 0x02); // ISP On
	}
	cis_write_cmd(0x40, 0xac, 0x01); // Set USB chip to start
}

void cis_usb_end()
{
	cis_start_fg = 0;
	do_exit = 1;
	cis_i2c_write(I2C_ADDR, 0x301, 0x00); // LED Off
	cis_i2c_write(I2C_ADDR, 0x200, 0x00); // ISP Off
	cis_write_cmd(0x40, 0xac, 0x00); // Set USB chip to stop
}

// write i2c data to mcu
void cis_write_reg(int reg1, unsigned char buff[], int count)
{
	libusb_control_transfer(
		devh, // libusb_device_handle * dev_handle,
		(uint8_t)64, // uint8_t request_type,
		(uint8_t)0xC4, // uint8_t bRequest,
		0x60, // uint16_t wValue,
		(uint16_t)reg1, // uint16_t wIndex,
		buff, // unsigned char* data,
		(uint16_t)count,  // uint16_t wLength,
		1000 // unsigned int timeout
	);
}

// write i2c data to mcu
void cis_i2c_write(int addr, int reg, int val)
{
	unsigned char buff[2];
	uint16_t count = 2;

	buff[0] = (val >> 8) & 0xFF;
	buff[1] = val & 0xFF;

	libusb_control_transfer(
		devh, // libusb_device_handle * dev_handle,
		(uint8_t)64, // uint8_t request_type,
		(uint8_t)0xC4, // uint8_t bRequest,
		(uint16_t)addr, // uint16_t wValue,
		(uint16_t)reg, // uint16_t wIndex,
		buff, // unsigned char* data,
		(uint16_t)count,  // uint16_t wLength,
		1000 // unsigned int timeout
	);
}

// read i2c data from mcu
int cis_read_reg(int reg1, unsigned char buff[], int count)
{
	libusb_control_transfer(
		devh, // libusb_device_handle * dev_handle,
		(uint8_t)192, // uint8_t request_type,
		(uint8_t)0xC3, // uint8_t bRequest,
		0x61, // uint16_t wValue,
		(uint16_t)reg1, // uint16_t wIndex,
		buff, // unsigned char* data,
		(uint16_t)count,  // uint16_t wLength,
		5000 // unsigned int timeout
	);
	return (buff[0] * 256 + buff[1]);
}

// read i2c data from mcu
int cis_i2c_read(int addr, int reg)
{
	unsigned char buff[2];
	uint16_t count = 2;

	buff[0] = 0x0;
	buff[1] = 0x0;
	libusb_control_transfer(
		devh, // libusb_device_handle * dev_handle,
		(uint8_t)192, // uint8_t request_type,
		(uint8_t)0xC3, // uint8_t bRequest,
		(uint16_t)(addr + 1), // uint16_t wValue,
		(uint16_t)reg, // uint16_t wIndex,
		buff, // unsigned char* data,
		count,  // uint16_t wLength,
		5000 // unsigned int timeout
	);
	return (buff[0] * 256 + buff[1]);
}

// receive command to usb
void cis_read_cmd(uint16_t cmd_no, uint16_t reg, uint16_t val)
{
	libusb_control_transfer(
		devh, // libusb_device_handle * dev_handle,
		(uint8_t)cmd_no, // uint8_t request_type, 
		(uint8_t)reg, // uint8_t bRequest,
		val, // uint16_t wValue,
		0x00, // uint16_t wIndex,
		usbBuff, // unsigned char* data,
		16,  // uint16_t wLength,
		1000 // unsigned int timeout
	);
}

// send command to usb
void cis_write_cmd(uint16_t cmd_no, uint16_t reg, uint16_t val)
{
	libusb_control_transfer(
		devh, // libusb_device_handle * dev_handle,
		(uint8_t)cmd_no, // uint8_t request_type,
		(uint8_t)reg, // uint8_t bRequest,
		val, // uint16_t wValue,
		0x00, // uint16_t wIndex,
		usbBuff, // unsigned char* data,
		16,  // uint16_t wLength,
		1000 // unsigned int timeout
	);
}