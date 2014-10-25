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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <errno.h>
#include <math.h>
#include "bcc_lib.h"

/* PCF port expander base address */
#define PCF_ADDR 0x20

const unsigned char idprom[] =
{
	0xaa, 0x55, 0x33, 0xee, 0x41, 0x31, 0x42, 0x43, 0x43, 0x00
};

/* .bit file header */
const char bit_hdr[] =
{
	0x00, 0x09, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x01
};

const char *bit_hdr_strings[] =
{
	"filename",
	"device",
	"date",
	"time"
};

const char *deviceid = "3s200avq100";

/* wrapper for fprintf(stderr, ...) to support verbose control */
void qprintf(bfpga *s, char *fmt, ...)
{
	va_list args;
	
	if(s->verbose)
	{
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
	}
}

/* set I2C slave address */
int i2c_set_slave_addr(int file, int address)
{
	if(ioctl(file, I2C_SLAVE, address) < 0)
		return -errno;
	else
		return 0;
}

/* low-level interface to I2C read/write */
static inline __s32 i2c_smbus_access(int file, char read_write, __u8 command, 
                                     int size, union i2c_smbus_data *data)
{
	struct i2c_smbus_ioctl_data args;

	args.read_write = read_write;
	args.command = command;
	args.size = size;
	args.data = data;
	return ioctl(file,I2C_SMBUS,&args);
}

/* get a single byte from an I2C slave */
int bcc_i2c_get(bfpga *s, int saddr)
{
	union i2c_smbus_data data;
	
	/* set address */
	if(i2c_set_slave_addr(s->i2c_file, saddr))
		return -1;

	/* get data */
	if (i2c_smbus_access(s->i2c_file,I2C_SMBUS_READ, 0,
	                     I2C_SMBUS_BYTE,&data))
		return -1;
	else
		return 0x0FF & data.byte;
}

/* set a single byte to an I2C slave */
int bcc_i2c_set(bfpga *s, int saddr, __u8 dat)
{
	/* set address */
	if(i2c_set_slave_addr(s->i2c_file, saddr))
		return -1;

	/* send data */
	return i2c_smbus_access(s->i2c_file,I2C_SMBUS_WRITE,dat,
	                        I2C_SMBUS_BYTE,NULL);
}

/* get PCF GPIO bit */
int bcc_i2c_pcf_rd(bfpga *s, int bit)
{
	return (bcc_i2c_get(s, PCF_ADDR+s->cape_addr) >> bit) & 1;
}

/* get PCF GPIO bit */
void bcc_i2c_pcf_wr(bfpga *s, int bit, int val)
{
	int oldval = bcc_i2c_get(s, PCF_ADDR+s->cape_addr) | 0xC0;		// don't clear uppers
	int hmask = 1<<bit;
	int lmask = ~hmask & 0xFF;
	int newval = (oldval & lmask) | ((val & 1) << bit);
	bcc_i2c_set(s, PCF_ADDR+s->cape_addr, newval);
}

/* SPI Transmit/Receive */
int bcc_spi_txrx(bfpga *s, uint8_t *tx, uint8_t *rx, __u32 len)
{
	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)tx,
		.rx_buf = (unsigned long)rx,
		.len = len,
		.delay_usecs = 0,
		.speed_hz = 2000000,
		.bits_per_word = 8,
	};
	
	return ioctl(s->spi_file, SPI_IOC_MESSAGE(1), &tr);
}

/* Board Addressable SPI Transmit/Receive */
/* uses PCF_AUX_0 to enable SPI slave */
int bcc_spi_txrx_addr(bfpga *s, uint8_t *tx, uint8_t *rx, __u32 len)
{
	int retval = 0;
	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)tx,
		.rx_buf = (unsigned long)rx,
		.len = len,
		.delay_usecs = 0,
		.speed_hz = 2000000,
		.bits_per_word = 8,
	};
	
	bcc_i2c_pcf_wr(s, PCF_AUX_0, 0);
	retval = ioctl(s->spi_file, SPI_IOC_MESSAGE(1), &tr);
	bcc_i2c_pcf_wr(s, PCF_AUX_0, 1);
	return retval;
}

/* check Cape EEPROM contents vs BCC known */
int bcc_chkprom(bfpga *s, const unsigned char *prom)
{
	char filename[256];
	int fd;
	struct eeprom_data data;
	uint8_t *byteptr = (uint8_t *)&data;
	
	/* form sysfs filename for accessing eeprom */
	snprintf(filename, 255, "/sys/bus/i2c/devices/%c-005%c/eeprom",
		s->i2c_busno + 48,
		s->cape_addr + 52);
	qprintf(s, "bcc_chkprom: eeprom sysfs filename - %s\n", filename);
	
	/* open EEPROM file */
	if(!(fd = open(filename, O_RDONLY)))
	{
		qprintf(s, "bcc_chkprom: No EEPROM SYSFS found\n");
		return -1;
	}
	
	/* read EEPROM contents */
	if(!read(fd, byteptr, sizeof(struct eeprom_data)))
	{
		qprintf(s, "bcc_chkprom: EEPROM file read error\n");		
		close(fd);
		return -1;
	}
	close(fd);
	
	/* dump contents */
	qprintf(s, "bcc_chkprom: header = 0x%02X 0x%02X 0x%02X 0x%02X\n",
		data.header[0],data.header[1],data.header[2],data.header[3]);
	qprintf(s, "bcc_chkprom: Rev = %c%c\n", data.revision[0],data.revision[1]);
	qprintf(s, "bcc_chkprom: Board Name = %s\n", data.board_name);
	qprintf(s, "bcc_chkprom: Version = %c%c%c%c\n",
		data.version[0],data.version[1],data.version[2],data.version[3]);
	qprintf(s, "bcc_chkprom: Part No = %s\n", data.part_number);
	
	/* Check header vs expected */
	if( (data.header[0] != prom[0]) ||
		(data.header[1] != prom[1]) ||
		(data.header[2] != prom[2]) ||
		(data.header[3] != prom[3]))
	{
		qprintf(s, "bcc_chkprom: EEPROM Header Mismatch\n");		
		return -1;
	}
	
	if(strncmp(data.board_name, "BCC", 32))
	{
		qprintf(s, "bcc_chkprom: EEPROM Board Name Mismatch\n");		
		return -1;
	}
		
	/* Check for the BCC Cape ID PROM */
	qprintf(s, "bcc_chkprom: found BCC S6 IDPROM\n");
	
	return 0;
}

/* initialize our FPGA interface */
bfpga *bcc_init(int i2c_bus, int spi_bus, int spi_add, int cape_addr, int verbose)
{
	bfpga *s;
	char filename[20];
	uint32_t speed = 10000000;
	uint8_t mode = 0;
	//uint8_t mode = SPI_CPHA;
	//uint8_t mode = SPI_CPOL;
	
	/* allocate the object */
	if((s = calloc(1, sizeof(bfpga))) == NULL)
	{
		qprintf(s, "bcc_init: Couldn't allocate bfpga object\n");
		goto fail0;
	}
	
	/* save bus / addr info */
	s->i2c_busno = i2c_bus;
	s->spi_busno = spi_bus;
	s->spi_devno = spi_add;
	s->cape_addr = cape_addr & 3;
	
	/* set verbose level */
	s->verbose = verbose;
	
	/* open I2C bus */
	sprintf(filename, "/dev/i2c-%d", i2c_bus);
	s->i2c_file = open(filename, O_RDWR);

	if(s->i2c_file < 0)
	{
		qprintf(s, "bcc_init: Couldn't open I2C device %s\n", filename);
		goto fail1;
	}
	else
		qprintf(s, "bcc_init: opened I2C device %s\n", filename);
		
	
	/* Check for the BCC Cape ID PROM */
	if(bcc_chkprom(s, idprom))
	{
		qprintf(s, "bcc_init: Error - IDPROM Mismatched\n");
		goto fail2;
	}
	else
		qprintf(s, "bcc_init: IDPROM Matched\n");
	
	/* diagnostid - check status of port expander */
	qprintf(s, "bcc_init: PCF reads 0x%02X\n", bcc_i2c_get(s, PCF_ADDR+s->cape_addr));

	/* set PCF AUX0 high */
	bcc_i2c_pcf_wr(s, PCF_AUX_0, 1);
	qprintf(s, "bcc_init: PCF reads 0x%02X\n", bcc_i2c_get(s, PCF_ADDR+s->cape_addr));

	/* Open the SPI port */
	sprintf(filename, "/dev/spidev%d.%d", spi_bus, spi_add);
	s->spi_file = open(filename, O_RDWR);
	
	if(s->spi_file < 0)
	{
		qprintf(s, "bcc_init: Couldn't open spi device %s\n", filename);
		goto fail2;
	}
	else
		qprintf(s, "bcc_init: opened spi device %s\n", filename);

	if(ioctl(s->spi_file, SPI_IOC_WR_MAX_SPEED_HZ, &speed) == -1)
	{
		qprintf(s, "bcc_init: Couldn't set SPI clock to %d Hz\n", speed);
		goto fail2;
	}
	else
		qprintf(s, "bcc_init: Set SPI clock to %d Hz\n", speed);
	
	if(ioctl(s->spi_file, SPI_IOC_WR_MODE, &mode) == -1)
	{
		qprintf(s, "bcc_init: Couldn't set SPI mode\n");
		goto fail2;
	}
	else
		qprintf(s, "bcc_init: Set SPI mode\n");
	
	/* Check if FPGA already configured */
	if(bcc_i2c_pcf_rd(s,PCF_FPGA_DONE)==0)
	{
		qprintf(s, "bcc_init: FPGA not already configured - DONE not high\n\r");
	}	
	
	/* success */
	return s;

	/* failure modes */
//fail3:
//	close(s->spi_file);		/* close the SPI device */
fail2:
	close(s->i2c_file);		/* close the I2C device */
fail1:
	free(s);				/* free the structure */
fail0:
	return NULL;
}

/* open a bitfile and check the header */
FILE *bfpga_open_bitfile(bfpga *s, char *bitfile, long *n)
{
	FILE *fd;
	char readbuf[READBUFSIZE], *cp;
	int read, j, d;
	
	/* open file or return error*/
	if(!(fd = fopen(bitfile, "r")))
	{
		qprintf(s, "bfpga_open_bitfile: open file %s failed\n\r", bitfile);
		return 0;
	}
	else
	{
		qprintf(s, "bfpga_open_bitfile: found bitstream file %s\n\r", bitfile);
	}

	/* Read file & send bitstream via SPI1 */
	qprintf(s, "bfpga_open_bitfile: parsing header\n\r");
	if( (read=fread(readbuf, sizeof(char), 13, fd)) == 13 )
	{
		/* init pointer to keep track */
		cp = readbuf;
		
		/* check / skip .bit header */
		for(j=0;j<13;j++)
		{
			if(bit_hdr[j] != *cp++)
			{
				qprintf(s, "bfpga_open_bitfile: .bit header mismatch\n\r");
				fclose(fd);
				return 0;
			}
		}
		qprintf(s, "bfpga_open_bitfile: found header\n\r");
	}
	else
	{
		qprintf(s, "bfpga_open_bitfile: .bit header truncated\n\r");
		fclose(fd);
		return 0;
	}
		
	/* Skip File header chunks */
	for(j=0;j<4;j++)
	{
		if( (read=fread(readbuf, sizeof(char), 3, fd)) == 3 )
		{
			/* init pointer to keep track */
			cp = readbuf;
		
			/* get 1 byte chunk desginator (a,b,c,d) */
			d = *cp++;
			
			/* compute chunksize */
			*n = *cp++;
			*n <<= 8;
			*n += *cp;
			
			/* read chunk */
			if( (read=fread(readbuf, sizeof(char), *n, fd)) == *n )
			{
				/* print chunk */
				qprintf(s, "bfpga_open_bitfile: chunk %c length %ld %s %s\n\r", d, *n, bit_hdr_strings[j], readbuf);
			}
			else
			{
				qprintf(s, "bfpga_open_bitfile: chunk data truncated\n\r");
				fclose(fd);
				return 0;
			}
			
			/* Check device type */
			if(j==1)
			{
				if(strcmp(readbuf, deviceid))
					qprintf(s, "bfpga_open_bitfile: Device != %s\n\r", deviceid);
				else
					qprintf(s, "bfpga_open_bitfile: Device == %s\n\r", deviceid);
			}
		}
		else
		{
			qprintf(s, "bfpga_open_bitfile: chunk header truncated\n\r");
			fclose(fd);
			return 0;
		}
	}
	
	if( (read=fread(readbuf, sizeof(char), 5, fd)) == 5 )
	{
		/* init pointer to keep track */
		cp = readbuf;
		
		/* Skip final chunk designator */
		cp++;
	
		/* compute config data size - modified for 16-bit int & char */
		*n = *cp++;
		*n <<= 8;
		*n += *cp++;
		*n <<= 8;
		*n += *cp++;
		*n <<= 8;
		*n += *cp++;
		qprintf(s, "bfpga_open_bitfile: config size = %ld\n\r", *n);
	}
	else
	{
		qprintf(s, "bfpga_open_bitfile: final chunk truncated\n\r");
		fclose(fd);
		return 0;
	}
	
	/* success */
	return fd;
}

/* Send a bitstream to the FPGA */
int bcc_cfg(bfpga *s, char *bitfile)
{
	FILE *fd;
    int read;
	long ct, n;
	unsigned char byte, rxbyte, dummybuf[READBUFSIZE];
	char readbuf[READBUFSIZE];

	/* open file or return error*/
	if(!(fd = bfpga_open_bitfile(s, bitfile, &n)))
	{
		qprintf(s, "bcc_cfg: open bitfile %s failed\n\r", bitfile);
		return 1;
	}
	else
	{
		qprintf(s, "bcc_cfg: found bitfile %s\n\r", bitfile);
	}

	/* pulse PROG_B low min 500 ns */
	bcc_i2c_pcf_wr(s,PCF_FPGA_PROG,0);			// drive low
	usleep(1);			// wait a bit
	
	/* Wait for INIT low */
	qprintf(s, "bcc_cfg: PROG low, Waiting for INIT low\n\r");
	while(bcc_i2c_pcf_rd(s,PCF_FPGA_INIT)==1)
	{
		asm volatile ("nop");	//"nop" means no-operation.  We don't want to do anything during the delay
	}
	
	/* Release PROG */
	bcc_i2c_pcf_wr(s,PCF_FPGA_PROG,1);			// set as hi
	
	/* Wait for INIT high */
	qprintf(s, "bcc_cfg: PROG high, Waiting for INIT high\n\r");
	while(bcc_i2c_pcf_rd(s,PCF_FPGA_INIT)==0)
	{
		asm volatile ("nop");	//"nop" means no-operation.  We don't want to do anything during the delay
	}

	/* wait 5us */
	usleep(5);
	qprintf(s, "bcc_cfg: Sending bitstream\n\r");
	
	/* Read file & send bitstream to FPGA via SPI */
	ct = 0;
	while( (read=fread(readbuf, sizeof(char), READBUFSIZE, fd)) > 0 )
	{
		/* Send bitstream */
		bcc_spi_txrx(s, (unsigned char *)readbuf, dummybuf, read);
		ct += read;
		
		/* diagnostic to track buffers */
		qprintf(s, ".");
		if(s->verbose)
			fflush(stdout);
		
		/* Check INIT - if low then fail */
		if(bcc_i2c_pcf_rd(s,PCF_FPGA_INIT)==0)
		{
			qprintf(s, "\n\rbcc_cfg: INIT low during bitstream send\n\r");
			fclose(fd);
			return 1;
		}
	}
	
	/* close file */
	qprintf(s, "\n\rbcc_cfg: sent %ld of %ld bytes\n\r", ct, n);
	qprintf(s, "bcc_cfg: bitstream sent, closing file\n\r");
	fclose(fd);
	
	/* send dummy data while waiting for DONE or !INIT */
 	qprintf(s, "bcc_cfg: sending dummy clocks, waiting for DONE or fail\n\r");
	byte = 0xFF;
	ct = 0;
	while((bcc_i2c_pcf_rd(s,PCF_FPGA_DONE)==0) && (bcc_i2c_pcf_rd(s,PCF_FPGA_INIT)==1))
	{
		/* Dummy - all ones */
		bcc_spi_txrx(s, &byte, &rxbyte, 1);
		ct++;
	}
 	qprintf(s, "bcc_cfg: %d dummy clocks sent\n\r", ct*8);
		
	/* return status */
	if(bcc_i2c_pcf_rd(s,PCF_FPGA_DONE)==0)
	{
		qprintf(s, "bcc_cfg: cfg failed - DONE not high\n\r");
		return 1;	// Done = 0 - error
	}
	else	
	{
		qprintf(s, "bcc_cfg: success\n\r");
		return 0;	// Done = 1 - OK
	}
}

/* Clean shutdown of our FPGA interface */
void bcc_delete(bfpga *s)
{		
	close(s->spi_file);		/* close the SPI device */
	close(s->i2c_file);		/* close the I2C device */
	free(s);				/* free the structure */
}
