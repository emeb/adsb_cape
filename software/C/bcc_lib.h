/* bcc_lib.c - Blank Canvas Cape FPGA board access library                   */
/* Copyright 2012-2014 Eric Brombaugh                                        */
/*   This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
    MA 02110-1301 USA.
*/

#ifndef __bcc_lib__
#define __bcc_lib__

#include <stdint.h>
#include <sys/types.h>
#include <linux/types.h>

#define I2C_SLAVE	0x0703	/* Change slave address			*/
#define I2C_SMBUS	0x0720	/* SMBus-level access */
#define I2C_SMBUS_READ	1
#define I2C_SMBUS_WRITE	0
#define I2C_SMBUS_BYTE		    1
#define I2C_SMBUS_BYTE_DATA	    2 

/* 
 * Data for SMBus Messages 
 */
#define I2C_SMBUS_BLOCK_MAX	32	/* As specified in SMBus standard */	
#define I2C_SMBUS_I2C_BLOCK_MAX	32	/* Not specified but we use same structure */
union i2c_smbus_data {
	__u8 byte;
	__u16 word;
	__u8 block[I2C_SMBUS_BLOCK_MAX + 2]; /* block[0] is used for length */
	                                            /* and one more for PEC */
};

/* This is the structure as used in the I2C_SMBUS ioctl call */
struct i2c_smbus_ioctl_data {
	char read_write;
	__u8 command;
	int size;
	union i2c_smbus_data *data;
};

/* PCF8574 I/O bit definitions */
#define PCF_FPGA_INIT 7			/* reads cfg progress - leave set high */ 
#define PCF_FPGA_DONE 6			/* high for success - leave set high */
#define PCF_FPGA_PROG 5			/* starts cfg - pulse low to begin */
#define PCF_AUX_3 3				/* aux I2C signals to FPGA */
#define PCF_AUX_2 2				/* aux I2C signals to FPGA */
#define PCF_AUX_1 1				/* aux I2C signals to FPGA */
#define PCF_AUX_0 0				/* aux I2C signals to FPGA */

/* state structure */
typedef struct
{
	int i2c_busno;		/* I2C bus number */
	int spi_busno;		/* SPI bus number */
	int spi_devno;		/* SPI device addr */
	int i2c_file;		/* I2C device */
	int spi_file;		/* SPI device */
	int cape_addr;		/* Cape Address */
	int verbose;		/* Verbose level */
} bfpga;

/* the structure used to store eeprom data */
struct eeprom_data
{
	uint8_t header[4];
	char revision[2];
	char board_name[32];
	char version[4];
	char manufacturer[16];
	char part_number[16];
	uint16_t num_pins;
	char serial_number[12];
	uint16_t pinmux[74];
	uint16_t current3v3;
	uint16_t currentVdd;
	uint16_t currentSys;
	uint16_t dcSupplied;
};

#define READBUFSIZE 512

int bcc_i2c_get(bfpga *s, int saddr);
int bcc_i2c_set(bfpga *s, int saddr, __u8 dat);
int bcc_i2c_pcf_rd(bfpga *s, int bit);
void bcc_i2c_pcf_wr(bfpga *s, int bit, int val);
int bcc_spi_txrx(bfpga *s, uint8_t *tx, uint8_t *rx, __u32 len);
int bcc_spi_txrx_addr(bfpga *s, uint8_t *tx, uint8_t *rx, __u32 len);
bfpga *bcc_init(int i2c_bus, int spi_bus, int spi_add, int cape_addr, int verbose);
FILE *bfpga_open_bitfile(bfpga *s, char *bitfile, long *n);
int bcc_cfg(bfpga *s, char *bitfile);
int bcc_pgm(bfpga *s, char *bitfile);
void bcc_delete(bfpga *s);

#endif
