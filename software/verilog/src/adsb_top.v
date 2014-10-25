// adsb_top.v - top-level FPGA wrapper for adsb_cape design
// 08-29-14 E. Brombaugh

module adsb_top
	(
		// Clock & Reset
		input FPGA_CLK20,		// 20 MHz clock
		input SYS_RESET,		// Reset from BBB
		
		// Primary MCU interface
		input SPI0_CS0,			// SPI CS
		input SPI0_MOSI,		// SPI data in
		output SPI0_MISO,		// SPI data out
		input SPI0_SCLK,		// SPI clock
		
		// AUX bits from I2C port expander
		input [4:0] FPGA_AUX,	// all one bus
				
		// ADC interface
		input adc_otr,			// overrange
		input [9:0] adc_dat,	// 10-bit unsigned data
		
		// LEDs
		output USER1,			// activity / debug
		output USER2,			// activity / debug
		output USER3,			// activity / debug
		
		// PRU interface - not used yet
		input [13:0] PRU1,	// all inputs to keep hi-z on pins
		
		// Misc BBB I/O - not used yet
		input MCASP0_ACLKX,	// all inputs to keep hi-z on pins
		input MCASP0_AXR0,	// all inputs to keep hi-z on pins
		input MCASP0_FSX,		// all inputs to keep hi-z on pins
		input MCASP0_AHCLKR,	// all inputs to keep hi-z on pins
		input MCASP0_FSR,		// all inputs to keep hi-z on pins
		input MCASP0_AHCLKX,	// all inputs to keep hi-z on pins
		input UART1_RXD,		// all inputs to keep hi-z on pins
		input UART1_TXD		// all inputs to keep hi-z on pins
	);
	
	// This should be unique so firmware knows who it's talking to
	parameter DESIGN_ID = 32'hAD5B0010;

	// 20 MHz clock input buffer
	wire clk;
	IBUFG
		uclk20buf(.I(FPGA_CLK20), .O(clk));
	
	// delayed / stretched Reset generator
	wire [3:0] rst;				// ADC POR delay
	FDCE #(.INIT(1'b0)) rst_bit0(.Q(rst[0]), .C(clk), .CE(1'b1), .CLR(1'b0), .D(1'b1));
	FDCE #(.INIT(1'b0)) rst_bit1(.Q(rst[1]), .C(clk), .CE(1'b1), .CLR(1'b0), .D(rst[0]));
	FDCE #(.INIT(1'b0)) rst_bit2(.Q(rst[2]), .C(clk), .CE(1'b1), .CLR(1'b0), .D(rst[1]));
	FDCE #(.INIT(1'b1)) rst_bit3(.Q(rst[3]), .C(clk), .CE(1'b1), .CLR(1'b0), .D(~rst[2]));
	assign reset = rst[3];
		
	// qualify SPI CS0 with FPGA_AUX0 to allow board-level addressing
	wire SPI_CS0_QUAL = SPI0_CS0 | FPGA_AUX[0];

	// SPI slave port
	wire [31:0] wdat;
	reg [31:0] rdat;
	wire [6:0] addr;
	wire re, we;
	spi_slave
		uspi(.clk(clk), .reset(reset),
			.spiclk(SPI0_SCLK), .spimosi(SPI0_MOSI),
			.spimiso(SPI0_MISO), .spicsl(SPI_CS0_QUAL),
			.we(we), .re(re), .wdat(wdat), .addr(addr), .rdat(rdat));
	
	// generate single-cycle read enable synced up to clk
	reg [3:0] re_sync;
	reg re_edge;
	always @(posedge clk)
		if(reset)
		begin
			re_sync <= 4'h0;
			re_edge <= 1'b0;
		end
		else
		begin
			re_sync <= {re_sync[2:0],re};
			re_edge <= ~re_sync[3] & re_sync[2];
		end	
	
	// Writeable registers
	reg [31:0] cnt_limit_reg;
	reg [31:0] stretch_reg;
	reg [9:0] mtl;
	reg noise_ride, det_ena;
	always @(posedge clk)
		if(reset)
		begin
			cnt_limit_reg <= 32'd40000000;	// 1 sec blink rate
			stretch_reg <= 32'd800000;		// pulse stretch period
			mtl <= 10'd620; 				// Min Trigger Level
			noise_ride <= 1'b0; 			// Enable noise riding MTL
			det_ena <= 1'b0; 				// Enable detector
		end
		else
		begin			
			if(we)
				case(addr)
					7'h01: cnt_limit_reg <= wdat;
					7'h02: stretch_reg <= wdat;
					7'h03: mtl <= wdat;
					7'h04: {noise_ride,det_ena} <= wdat;
				endcase
		end
	
	// FIFO read pulses
	wire fifo_re = re_edge & (addr == 7'h06);
	
	// Stat pulses
	wire stat_freeze = re_edge & (addr == 7'h07);
	reg [7:0] stat_reset_pipe;
	always @(posedge clk)
		stat_reset_pipe <= {stat_reset_pipe[6:0],(re_edge & (addr == 7'h09))};
	wire stat_reset = stat_reset_pipe[7];
		
	// reclock the adc data
	reg [9:0] log_video;
	always @(posedge clk)
		log_video <= adc_dat;
		
	// instantiate the ADS-B Receiver
	wire trigger, data_start;
	wire ena_out, data, conf, done;
	wire [9:0] thresh, noise_out;
	wire watchdog_reset;
	wire slice_data;
	wire retrigger;
	adsb_rx #(.width(10))
		uADSBRX(.clock(clk), .reset(reset), .ena(dec_ena),
			.det_ena(det_ena), .noise_ride(noise_ride),
			.mtl(mtl), .logmag(log_video),
			.trigger(trigger), .data_start(data_start),
			.ena_out(ena_out), .data(data), .conf(conf), .done(done),
			.watchdog_reset(watchdog_reset),
			.thresh(thresh), .noise_out(noise_out),
			.slice_data(slice_data), .retrigger_out(retrigger)
		);
	
	// format the data for writing to the FIFO
	wire write_ena;
	wire [31:0] write_data;
	wire len_err;
	wire [2:0] fb_state;
	fifo_buffer #(.width(10))
		UFIFOBuffer(.clock(clk), .reset(watchdog_reset), .ena(dec_ena),
			.thresh(thresh), .data_start(data_start),
			.ena_out(ena_out), .data(data), .conf(conf), .done(done),
			.write_data(write_data), .write_ena(write_ena),
			.len_err(len_err), .state_out(fb_state)
		);
	
	// FIFO from CoreGen
	wire [31:0] fifo_data;
	wire [7:0] fifo_status;
	wire fifo_full;
	fifo_generator_v9_3
		UFIFO(.rst(reset),
			.wr_clk(clk), .din(write_data), .wr_en(write_ena),
			.rd_clk(clk), .rd_en(fifo_re), .dout(fifo_data),
			.full(fifo_full), .empty(), .rd_data_count(fifo_status)
		);
	
	// statistics counters
	reg len_err_dly, stat_hold;
	reg [15:0] stat_prscl, stat_time, stat_trig, stat_start, stat_err;
	always @(posedge clk)
	begin
		if(reset | stat_reset)
		begin
			stat_prscl <= 16'd0;
			stat_time <= 16'd0;
			stat_trig <= 16'd0;
			stat_start <= 16'd0;
			stat_err <= 16'd0;
			stat_hold <= 1'b0;
			len_err_dly <= 1'b0;
		end
		else
		begin
			if(dec_ena)
			begin
				if(!stat_hold)
				begin
					// only update stats if not frozen
					if(trigger)
						stat_trig <= stat_trig + 1;
						
					if(data_start)
						stat_start <= stat_start + 1;
					
					if(len_err | len_err_dly)
						stat_err <= stat_err + 1;
						
					// time prescaler & counter
					stat_prscl <= stat_prscl + 1;
					if(stat_prscl == 19999)
					begin
						// time increments at 1ms rate
						stat_time <= stat_time + 1;
						if(stat_time == 16'hffff)
						begin
							// reset all at 65.536 sec rollover
							stat_trig <= 16'd0;
							stat_start <= 16'd0;
							stat_err <= 16'd0;
						end	// stat_time
					end	// stat_prscl
				end // stat_hold
			end // dec_ena
			
			// delay len_err to catch narrow pulses
			len_err_dly <= len_err;
			
			// catch stat_freeze
			stat_hold <= stat_hold | stat_freeze;
		end // reset
	end // posedge clk
	
	// Clock divider & Visible clock diagnostics on LED
	clk_diag //#(.period(16))
		uclkdiag(.clk(clk), .reset(reset),
			.period(cnt_limit_reg), .tc(), .out(USER1));
		
	// stretch pulses for LED drive from Trigger pulse
	pulse_stretch
		uTrigStretch(.clk(clk), .reset(reset),
			.period(stretch_reg),
			.in(trigger), .out(USER2)
		);
	
	// stretch pulses for LED drive from data_start pulse
	pulse_stretch
		uDSStretch(.clk(clk), .reset(reset),
			.period(stretch_reg),
			.in(data_start), .out(USER3)
		);
	
	// readback
	wire [10:0] dpram_rdat;
	always @(*)
		case(addr)
			7'h00: rdat = DESIGN_ID;
			7'h01: rdat = cnt_limit_reg;
			7'h02: rdat = stretch_reg;
			7'h03: rdat = mtl;
			7'h04: rdat = {noise_ride,det_ena};
			7'h05: rdat = fifo_status;
			7'h06: rdat = fifo_data;
			7'h07: rdat = {stat_time,stat_trig};
			7'h08: rdat = {stat_start,stat_err};
			7'h0A: rdat = noise_out;
			7'h0E: rdat = FPGA_AUX;
			default: rdat = 32'd0;
		endcase
endmodule
