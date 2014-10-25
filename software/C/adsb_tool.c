/* adsb_tool.c - Blank Canvas Cape S3 ADS-B testing tool                     */
/* Copyright 2014 Eric Brombaugh                                             */
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
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include "bcc_lib.h"
#include "anet.h"

#define VERSION "0.4"
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* choose BBW or BBB/BS */
#ifdef BBW_DEVICE
#define I2C_BUS 3
#else
#define I2C_BUS 1
#endif

bfpga *bs;
clock_t clk, prev_clk;
int verbose=0;
char *regchar = "t";
int regmap[] = {6, 8, 9, 10};
FILE *dfile;

static void help(void) __attribute__ ((noreturn));

static void help(void)
{
	fprintf(stderr,
	    "Usage: adsb_tool [-v][-V]\n"
		"  -a cape at addr [0-3] (default = 0)\n"
		"  -r raw mode\n"
		"  -v enables verbose progress messages\n"
		"  -V prints the tool version\n");
	exit(1);
}

void reg_write(int addr, int data)
{
	int ret;
	uint8_t tx[] = {0x00, 0x00, 0x00, 0x00, 0x00};
	uint8_t rx[ARRAY_SIZE(tx)] = {0, };
	
	/* Build message */
	tx[0] = addr & 0x7f;	// set address
	if(verbose)
		fprintf(stderr, "Write 0x%02X 0x%08X\n", addr, data);
	tx[1] = (data >> 24) & 0xff;
	tx[2] = (data >> 16) & 0xff;
	tx[3] = (data >>  8) & 0xff;
	tx[4] = (data >>  0) & 0xff;

	/* send a message */
	ret = bcc_spi_txrx_addr(bs, tx, rx, ARRAY_SIZE(tx));
	if (ret == 1)
	{
		fprintf(stderr, "Can't send spi message\n");
	}
	else if(verbose)
	{
		/* Dump RX Message */
		fprintf(stderr, "Sent: ");
		for (ret = 0; ret < ARRAY_SIZE(tx); ret++)
		{
			fprintf(stderr, "%.2X ", tx[ret]);
		}
		fprintf(stderr, "\n");
		fprintf(stderr, "Received: ");
		for (ret = 0; ret < ARRAY_SIZE(rx); ret++)
		{
			fprintf(stderr, "%.2X ", rx[ret]);
		}
		fprintf(stderr, "\n");
	}
}

int reg_read(int addr)
{
	int ret, i;
	uint8_t tx[] = {0x00, 0x00, 0x00, 0x00, 0x00};
	uint8_t rx[ARRAY_SIZE(tx)] = {0, };
	
	/* Build message */
	tx[0] = addr & 0x7f;	// set address
	tx[0] |= 0x80;
	if(verbose)
		fprintf(stderr, "Read 0x%02X\n", addr);

	/* send a message */
	ret = bcc_spi_txrx_addr(bs, tx, rx, ARRAY_SIZE(tx));
	if (ret == 1)
	{
		fprintf(stderr, "Can't send spi message\n");
		return -1;
	}
	else
	{
		ret = (rx[1]<<24) | (rx[2]<<16) | (rx[3]<<8) | (rx[4]);
		if(verbose)
		{
			/* Dump RX Message */
			fprintf(stderr, "Sent: ");
			for (i = 0; i < ARRAY_SIZE(tx); i++)
			{
				fprintf(stderr, "%.2X ", tx[i]);
			}
			fprintf(stderr, "\n");
			fprintf(stderr, "Received: ");
			for (i = 0; i < ARRAY_SIZE(rx); i++)
			{
				fprintf(stderr, "%.2X ", rx[i]);
			}
			fprintf(stderr, "\n");
			fprintf(stderr, "Value: %d\n", ret);
		}
		return ret;
	}
}

/* ============================= Networking =================================
 * Note: here we risregard any kind of good coding practice in favor of
 * extreme simplicity, that is:
 *
 * 1) We only rely on the kernel buffers for our I/O without any kind of
 *    user space buffering.
 * 2) We don't register any kind of event handler, from time to time a
 *    function gets called and we accept new connections. All the rest is
 *    handled via non-blocking I/O and manually pulling clients to see if
 *    they have something new to share with us when reading is needed.
 */

#define MODES_DEBUG_NET (1<<5)

#define MODES_NET_MAX_FD 1024
#define MODES_NET_OUTPUT_SBS_PORT 30003
#define MODES_NET_OUTPUT_RAW_PORT 30002
#define MODES_NET_INPUT_RAW_PORT 30001
#define MODES_NET_HTTP_PORT 8080
#define MODES_CLIENT_BUF_SIZE 1024
#define MODES_NET_SNDBUF_SIZE (1024*64)

/* Program network state (ganked from dump1090). */
struct {
    /* Networking */
    char aneterr[ANET_ERR_LEN];
    struct client *clients[MODES_NET_MAX_FD]; /* Our clients. */
    int maxfd;                      /* Greatest fd currently active. */
    int sbsos;                      /* SBS output listening socket. */
    int ros;                        /* Raw output listening socket. */
    int ris;                        /* Raw input listening socket. */
    int https;                      /* HTTP listening socket. */
	
    /* Configuration */
    int debug;                      /* Debugging mode. */
	
	/* Statistics */
    long long stat_sbs_connections;
} Modes;

#define MODES_NET_SERVICE_RAWO 0
#define MODES_NET_SERVICE_RAWI 1
#define MODES_NET_SERVICE_HTTP 2
#define MODES_NET_SERVICE_SBS 3
#define MODES_NET_SERVICES_NUM 1
struct {
    char *descr;
    int *socket;
    int port;
} modesNetServices[MODES_NET_SERVICES_NUM] = {
    {"Raw TCP output", &Modes.ros, MODES_NET_OUTPUT_RAW_PORT},
    //{"Raw TCP input", &Modes.ris, MODES_NET_INPUT_RAW_PORT},
    //{"HTTP server", &Modes.https, MODES_NET_HTTP_PORT},
    //{"Basestation TCP output", &Modes.sbsos, MODES_NET_OUTPUT_SBS_PORT}
};

/* Structure used to describe a networking client. */
struct client {
    int fd;         /* File descriptor. */
    int service;    /* TCP port the client is connected to. */
    char buf[MODES_CLIENT_BUF_SIZE+1];    /* Read buffer. */
    int buflen;                         /* Amount of data on buffer. */
};

/* Networking "stack" initialization. */
void modesInitNet(void) {
    int j;

    memset(Modes.clients,0,sizeof(Modes.clients));
    Modes.maxfd = -1;

    for (j = 0; j < MODES_NET_SERVICES_NUM; j++) {
        int s = anetTcpServer(Modes.aneterr, modesNetServices[j].port, NULL);
        if (s == -1) {
            fprintf(stderr, "Error opening the listening port %d (%s): %s\n",
                modesNetServices[j].port,
                modesNetServices[j].descr,
                strerror(errno));
            exit(1);
        }
        anetNonBlock(Modes.aneterr, s);
        *modesNetServices[j].socket = s;
    }

    signal(SIGPIPE, SIG_IGN);
}

/* This function gets called from time to time when the decoding thread is
 * awakened by new data arriving. This usually happens a few times every
 * second. */
void modesAcceptClients(void) {
    int fd, port;
    unsigned int j;
    struct client *c;

    for (j = 0; j < MODES_NET_SERVICES_NUM; j++) {
        fd = anetTcpAccept(Modes.aneterr, *modesNetServices[j].socket,
                           NULL, &port);
        if (fd == -1) {
            if (Modes.debug & MODES_DEBUG_NET && errno != EAGAIN)
                printf("Accept %d: %s\n", *modesNetServices[j].socket,
                       strerror(errno));
            continue;
        }

        if (fd >= MODES_NET_MAX_FD) {
            close(fd);
            return; /* Max number of clients reached. */
        }

        anetNonBlock(Modes.aneterr, fd);
        c = malloc(sizeof(*c));
        c->service = *modesNetServices[j].socket;
        c->fd = fd;
        c->buflen = 0;
        Modes.clients[fd] = c;
        anetSetSendBuffer(Modes.aneterr,fd,MODES_NET_SNDBUF_SIZE);

        if (Modes.maxfd < fd) Modes.maxfd = fd;
        if (*modesNetServices[j].socket == Modes.sbsos)
            Modes.stat_sbs_connections++;

        j--; /* Try again with the same listening port. */

        if (Modes.debug & MODES_DEBUG_NET)
            printf("Created new client %d\n", fd);
    }
}

/* On error free the client, collect the structure, adjust maxfd if needed. */
void modesFreeClient(int fd) {
    close(fd);
    free(Modes.clients[fd]);
    Modes.clients[fd] = NULL;

    if (Modes.debug & MODES_DEBUG_NET)
        printf("Closing client %d\n", fd);

    /* If this was our maxfd, scan the clients array to find the new max.
     * Note that we are sure there is no active fd greater than the closed
     * fd, so we scan from fd-1 to 0. */
    if (Modes.maxfd == fd) {
        int j;

        Modes.maxfd = -1;
        for (j = fd-1; j >= 0; j--) {
            if (Modes.clients[j]) {
                Modes.maxfd = j;
                break;
            }
        }
    }
}

/* Send the specified message to all clients listening for a given service. */
void modesSendAllClients(int service, void *msg, int len) {
    int j;
    struct client *c;

    for (j = 0; j <= Modes.maxfd; j++) {
        c = Modes.clients[j];
        if (c && c->service == service) {
            int nwritten = write(j, msg, len);
            if (nwritten != len) {
                modesFreeClient(j);
            }
        }
    }
}

int parse_pkt(unsigned int *pkt, unsigned int pkt_cnt)
{
	int i, j, k, l, m, n;
	unsigned char data[14], conf[14];
	char msg[128], *p = msg;
	
	/* first check for valid length */
	if(pkt_cnt == 5)
	{
		l=56;
		//fprintf(stdout, "\nShort\n");
	}
	else if(pkt_cnt == 8)
	{
		l=112;
		//fprintf(stdout, "\nLong\n");
	}
	else
	{
		//fprintf(stdout, ".");
		//fflush(stdout);
		return 1;
	}
	
	/* next, separate out data and confidence bits into byte arrays */
	k=0;
	for(i=0;i<l;i++)
	{
		j = i>>4;					// input word index
		k = i>>3;					// output byte index
		m = 30-(2*(i&0xF));			// input bit pair index
		n = 7-(i&0x7);				// output bit index
		if(n==7)
			data[k] = conf[k] = 0;	// clear destination byte		
		data[k] = data[k] | (((pkt[j+1]>>(m+1))&1)<<n);
		conf[k] = conf[k] | (((pkt[j+1]>>(m+0))&1)<<n);
		//printf("% 3d % 3d % 3d % 3d % 3d % 3d\n", i, j, k, l, m, n);
	}
	l = l/8;
	
#if 0
	/* dump out data & confidence on adjacent lines */
	fprintf(stdout, "D: ");
	for(i=0;i<l;i++)
		fprintf(stdout, "%02X", data[i]);
	fprintf(stdout, "\n");
	fprintf(stdout, "C: ");
	for(i=0;i<l;i++)
		fprintf(stdout, "%02X", conf[i]);
	fprintf(stdout, "\n");
	fflush(stdout);
#endif
	
	// can put some EDAC here, but for now just send raw
	
	// format the std msg
    *p++ = '*';
    for (j = 0; j < l; j++) {
        sprintf(p, "%02X", data[j]);
        p += 2;
    }
    *p++ = ';';
    *p++ = '\n';
	
	// send msg
    modesSendAllClients(Modes.ros, msg, p-msg);
	
	if(dfile)
	{
		*p++ = 0;
		fprintf(dfile, "%s", msg);
	}
	
	return 0;
}

int main(int argc, char **argv)
{
	int flags=0, raw = 0, cape_addr = 0;
	unsigned int fifo_status, fifo_data, pkt[8], pkt_cnt;
	unsigned int sw_frame_cnt, hw_frame_cnt, prev_sw_frame_cnt, prev_hw_frame_cnt;
	unsigned int stat_time, stat_trig, stat_start, stat_err, stat, clk_dec;
	
#if 1
	/* handle (optional) flags first */
	while (1+flags < argc && argv[1+flags][0] == '-')
	{
		switch (argv[1+flags][1])
		{
		case 'a':
			if (2+flags < argc)
				cape_addr = (atoi(argv[flags+2]) & 3);
			flags++;
			break;
		case 'r':
			raw = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'V':
			fprintf(stderr, "adsb_tool version %s\n", VERSION);
			exit(0);
		default:
			fprintf(stderr, "Error: Unsupported option "
				"\"%s\"!\n", argv[1+flags]);
			help();
			exit(1);
		}
		flags++;
	}

	/* open up hardware */
	if((bs = bcc_init(I2C_BUS, 1, 0, cape_addr, verbose)) == NULL)
	{
		fprintf(stderr, "Couldn't access hardware\n");
		exit(2);
	}
	
	/* init networking */
	modesInitNet();
	
	/* diagnostic file */
	if(raw)
	{
		if(!(dfile = fopen("raw_msg.txt", "w")))
		{
			fprintf(stderr, "Couldn't open raw_msg.txt for write\n");
			bcc_delete(bs);
			exit(3);
		}
	}
	else
		dfile = NULL;
	
	/* Poll the hardware */
	stat_time = stat_trig = stat_start = stat_err = 0;
	pkt_cnt = sw_frame_cnt = 0;
	prev_sw_frame_cnt = prev_hw_frame_cnt = 0;
	prev_clk = clock();
	clk_dec = 10;
	while(1)
	{
		/* check for new data from the receiver */
		if((fifo_status = reg_read(5))>=1)
		{
			//fprintf(stdout, "FIFO Status: %d\n", fifo_status);
			
			while(fifo_status--)
			{
				fifo_data = reg_read(6);
				if((fifo_data >> 12) == 0xAD50B)
				{
					if(!parse_pkt(pkt, pkt_cnt))
						sw_frame_cnt++;
					pkt_cnt = 0;
					//fprintf(stdout, "------------------\n");
				}
				if(pkt_cnt < 8)
					pkt[pkt_cnt++] = fifo_data;
				
				//fprintf(stdout, "%08X\n", fifo_data);
			}
		}
		
		/* check for socket request */
		clk = clock();
		if((clk - prev_clk) > (CLOCKS_PER_SEC/10))
		{
			prev_clk = clk;
			modesAcceptClients();
			clk_dec--;
			if(clk_dec == 0)
			{
				// update stats
				clk_dec = 10;
				stat = reg_read(7);
				fprintf(stdout, " Time:% 6d  Trig:% 6d ", (stat>>16), (stat&0xFFFF));
				stat_time += (stat>>16);
				stat_trig += (stat&0xFFFF);
				stat = reg_read(8);
				fprintf(stdout, " Start:% 6d  Errs:% 6d ", (stat>>16), (stat&0xFFFF));
				stat_start += (stat>>16);
				stat_err += (stat&0xFFFF);
				stat = reg_read(9);
				hw_frame_cnt = (stat_start-stat_err);
				fprintf(stdout, "HW Frames:% 5d ", hw_frame_cnt - prev_hw_frame_cnt);
				fprintf(stdout, "SW Frames:% 5d ", sw_frame_cnt - prev_sw_frame_cnt);
				fprintf(stdout, "\n");
				prev_sw_frame_cnt = sw_frame_cnt;
				prev_hw_frame_cnt = hw_frame_cnt;
				//fprintf(stdout, "cTime:% 6d cTrig:% 6d ", stat_time, stat_trig);
				//fprintf(stdout, "cStart:% 6d cErrs:% 6d\n", stat_start, stat_err);
				fprintf(stdout, "Trig/s:% 8.2f ", 1000.0*(double)stat_trig/(double)stat_time);
				fprintf(stdout, "Start/s:% 8.2f ", 1000.0*(double)stat_start/(double)stat_time);
				fprintf(stdout, "Err/Start:% 8.2f ", (double)stat_err/(double)stat_start);
				fprintf(stdout, "HW Frame/min:% 8.2f ", 60000*(double)(hw_frame_cnt)/(double)stat_time);
				fprintf(stdout, "SW Frame/min:% 8.2f ", 60000*(double)(sw_frame_cnt)/(double)stat_time);
				fprintf(stdout, "\n");
				stat = reg_read(10);
				fprintf(stdout, "Noise Estimate:% 5d ", stat);
				fprintf(stdout, "\n");
				fflush(stdout);
			}
		}
	}
	
	/* All done */
	bcc_delete(bs);
#else

	pkt[0] = 0xAD50B276;
	pkt[1] = 0x5555DD57;
	pkt[2] = 0xD577575D;
	pkt[3] = 0xF57D5DD7;
	pkt[4] = 0xFFF70000;
	parse_pkt(pkt, 5);
#endif

	return 0;
}
