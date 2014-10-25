OBJS = bcc_lib.o

# enable BBW_DEVICE for Beaglebone White
#CFLAGS = -Wall -DBBW_DEVICE
CFLAGS = -Wall

default: bcc_tool

bcc_tool: $(OBJS) bcc_tool.c 
	$(CC) $^ -o bcc_tool -lm

adsb_tool: $(OBJS) anet.o adsb_tool.c 
	$(CC) $^ -o adsb_tool -lm

clean:
	rm -f *.o *~ core bcc_tool adsb_tool
