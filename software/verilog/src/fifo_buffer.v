// fifo_buffer.v - ads-b receiver data interface
// 8-4-14 E. Brombaugh

module fifo_buffer
	#(
		parameter width = 10
	)
	(
		input clock,						// main system clock
		input reset,						// main system reset
		input ena,							// enable input
		input [width-1:0] thresh,			// RX Thresh Level
		input data_start,					// start new header
		input ena_out,						// data/conf enable
		input data,							// bit data
		input conf,							// confidence
		input done,							// frame done
		output [31:0] write_data,			// write data to fifo
		output write_ena,					// write enable to fifo
		output len_err,						// packet length error
		output[2:0] state_out				// state machine
	);
	
	// state machine runs FIFO fill process
	reg [2:0] state;
	reg [2:0] word_cnt;
	reg write_ena_z;
	reg [1:0] source;
	reg shifter_full;
	reg done_hold;
	reg len_err_z;
	always @(posedge clock)
	begin
		if(reset)
		begin
			state <= 3'b000;	// wait for start
			word_cnt <= 3'b000;	// number of words this packet
			write_ena_z <= 1'b0;	// no write
			source <= 2'b00;		// header source
			done_hold <= 1'b0;	// clear done hold
			len_err_z <= 1'b0;
		end
		else
		begin
			// latch done in case state machine isn't looking when it arrives
			if(done & ena)
				done_hold <= 1'b1;
			
			// process states
			case(state)
				3'b000:	// wait for data start
				begin
					source <= 2'b00;
					len_err_z <= 1'b0;
					done_hold <= 1'b0;
					if(data_start & ena)
					begin
						// data start found so write header
						state <= 3'b001;
						write_ena_z <= 1'b1;
						word_cnt <= word_cnt + 3'h1;
					end
				end
				
				3'b001:	// write header
				begin
					// always disable write, select shifter source and
					// advance to wait for data
					state <= 3'b010;
					write_ena_z <= 1'b0;
					source <= 2'b01;
				end
				
				3'b010:	// wait for data
				begin
					if((done & ena) | done_hold)
					begin
						// done bit seen so write what we've got so far and
						// leave
						state <= 3'b100;
						source <= 2'b10;
						write_ena_z <= 1'b1;
						done_hold <= 1'b0;
						word_cnt <= word_cnt + 3'h1;
					end
					else if(shifter_full)
					begin
						// shifter is full so write shifter and come back
						state <= 3'b011;
						write_ena_z <= 1'b1;
						word_cnt <= word_cnt + 3'h1;
					end
				end
				
				3'b011:	// write shifter & keep waiting for data
				begin
					// always disable write, select shifter source and
					if(word_cnt == 3'h0)
						state <= 3'b101; // wait for done
					else
						state <= 3'b010; // keep waiting for data

					write_ena_z <= 1'b0;
				end
				
				3'b100:	// write shifter & finish
				begin
					// always disable write, select header source and
					// return to start
					state <= 3'b000;
					word_cnt <= 3'b000;
					write_ena_z <= 1'b0;
					if(word_cnt != 3'b101)
						len_err_z <= 1'b1;
				end
				
				3'b101:	// all words received, wait for done
				begin
					if((done & ena) | done_hold)
					begin
						// reset and return to start
						state <= 3'b000;
						source <= 2'b00;					
						word_cnt <= 3'b000;
					end
				end
				
				default:// all others
				begin
					// reset and return to start
					state <= 3'b000;
					word_cnt <= 3'b000;
					write_ena_z <= 1'b0;
					source <= 2'b00;					
				end
			endcase
		end
	end
	
	// data shifter accumulates bits for storage
	reg [3:0] shift_count;
	reg [31:0] shifter, shifter_hold;
	always @(posedge clock)
	begin
		if(reset)
		begin
			shift_count <= 3'b000;
			shifter_full <= 1'b0;
			shifter <= 32'h00000000;
			shifter_hold <= 32'h00000000;
		end
		else
		begin
			if(data_start & ena)
			begin
				// always clear the shifter at data start
				shift_count <= 3'b000;
				shifter_full <= 1'b0;
				shifter <= 32'h00000000;
				shifter_hold <= 32'h00000000;
			end
			else if(ena_out & ena)
			begin
				// shift in new data and advance counter
				shift_count <= shift_count + 4'h1;
				
				if(shift_count == 4'hF)
				begin
					// grab full word and reset shifter
					shifter_full <= 1'b1;
					shifter_hold <= {shifter[29:0],data,conf};
					shifter <= 32'h00000000;
				end
				else
					shifter <= {shifter[29:0],data,conf};
			end
			else
				shifter_full <= 1'b0;

		end
	end
	
	// source mux
	reg [31:0] write_data_z;
	always @(*)
		case(source)
			2'b00: write_data_z = {20'hAD50B,2'b00,thresh};
			2'b01: write_data_z = shifter_hold;
			2'b10: write_data_z = {shifter[15:0],16'h0000};
			2'b11: write_data_z = {shifter[15:0],16'h0000};
		endcase
	
	// assign outputs
	assign write_data = write_data_z;
	assign write_ena = write_ena_z;
	assign len_err = len_err_z;
	assign state_out = state;
endmodule
