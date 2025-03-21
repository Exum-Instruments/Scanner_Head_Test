#pragma once
#ifndef CIS_CORE_H
#define CIS_CORE_H

#define I2C_ADDR 0x60

#include "libusb.h"

extern unsigned long num_xfer;
extern uint8_t cis_start_fg;
extern int done;

int cis_usb_init();
void cis_usb_deinit();
void LIBUSB_CALL cb_xfr(struct libusb_transfer* xfr);
void cis_usb_config();
void cis_usb_start();
void cis_usb_end();
void cis_i2c_write(int addr, int reg, int val);
int cis_i2c_read(int addr, int reg);
void cis_read_cmd(uint16_t cmd_no, uint16_t reg, uint16_t val);
void cis_write_cmd(uint16_t cmd_no, uint16_t reg, uint16_t val);

#endif  // CIS_CORE_H