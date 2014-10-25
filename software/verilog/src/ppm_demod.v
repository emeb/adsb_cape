// ppm_demod.v - ppm demodulator for ads-b receiver
// 8-2-14 E. Brombaugh

module ppm_demod
	#(
		parameter width = 10
	)
	(
		input clock,						// main system clock
		input reset,						// main system reset
		input ena,							// enable input
		input [width-1:0] logmag,			// log magnitude data input
		input trigger,						// trigger input
		input slice_data,					// sliced data input
		output data_start,					// data start output
		output ena_out,						// bitrate enable output
		output data,						// bit data output
		output conf,						// confidence output
		output done							// frame done output
	);
	
	parameter CORR_THRESH = 135;
	parameter PIPE_LENGTH = 5;
	parameter OVERSAMPLE = 10;
	parameter Smpl1Time = OVERSAMPLE/2;
	parameter Smpl0Time = OVERSAMPLE + Smpl1Time;
	parameter BIT_THRESH = 8; // round up 3*20/8 for conf from DO-260
	
	// function to compute absolute value
	function [5:0] absolute (input signed [5:0] in);
		begin
			if(in[5])
				absolute = -in;
			else
				absolute = in;
		end
	endfunction
	
	// Correlation reference is a constant
	parameter [15:0] Preamble = 16'b1010000101000000;
	wire [159:0] corr_ref;
	generate
		genvar gN;
		for(gN=0;gN<16;gN=gN+1)
		begin:vec_repl_blk
			assign corr_ref[(OVERSAMPLE-1)+gN*10:gN*OVERSAMPLE] = {OVERSAMPLE{Preamble[gN]}};
		end
	endgenerate
		
	// correlator pipeline
	reg [159:0] corr_pipe;
	always @(posedge clock)
		if(reset)
			corr_pipe <= {160{1'b0}};
		else if(ena)
		begin
			if(trigger)
				corr_pipe <= {160{1'b0}};
			else
				corr_pipe <= {corr_pipe[158:0],slice_data};
		end
	
	// correlate
	wire [159:0] corr_bits = ~(corr_pipe ^ corr_ref);
	
	// sum up the correlation bits
	// first rank & register
	reg [1:0] corr_sum1[79:0];
	generate
		genvar gO;
		for(gO=0;gO<80;gO=gO+1)
		begin:corr_sum1_blk
			always @(posedge clock)
				if(reset)
					corr_sum1[gO] <= 2'b0;
				else 
					corr_sum1[gO] <= {1'b0,corr_bits[1+2*gO]} + {1'b0,corr_bits[2*gO]};
		end
	endgenerate
		
	// second rank
	wire [2:0] corr_sum2[39:0];
	generate
		genvar gP;
		for(gP=0;gP<40;gP=gP+1)
		begin:corr_sum2_blk
			assign corr_sum2[gP] = {1'b0,corr_sum1[1+2*gP]} + {1'b0,corr_sum1[2*gP]};
		end
	endgenerate
	
	// third rank
	wire [3:0] corr_sum3[19:0];
	generate
		genvar gQ;
		for(gQ=0;gQ<20;gQ=gQ+1)
		begin:corr_sum3_blk
			assign corr_sum3[gQ] = {1'b0,corr_sum2[1+2*gQ]} + {1'b0,corr_sum2[2*gQ]};
		end
	endgenerate
	
	// fourth rank
	wire [4:0] corr_sum4[9:0];
	generate
		genvar gR;
		for(gR=0;gR<10;gR=gR+1)
		begin:corr_sum4_blk
			assign corr_sum4[gR] = {1'b0,corr_sum3[1+2*gR]} + {1'b0,corr_sum3[2*gR]};
		end
	endgenerate
	
	// fifth rank & register
	reg [5:0] corr_sum5[4:0];
	generate
		genvar gS;
		for(gS=0;gS<5;gS=gS+1)
		begin:corr_sum5_blk
			always @(posedge clock)
				if(reset)
					corr_sum5[gS] <= 6'd0;
				else	
					corr_sum5[gS] <= {1'b0,corr_sum4[1+2*gS]} + {1'b0,corr_sum4[2*gS]};
		end
	endgenerate
		
	// sixth rank
	wire [6:0] corr_sum6[2:0];
	generate
		genvar gT;
		for(gT=0;gT<2;gT=gT+1)
		begin:corr_sum6_blk
			assign corr_sum6[gT] = {1'b0,corr_sum5[1+2*gT]} + {1'b0,corr_sum5[2*gT]};
		end
		assign corr_sum6[2] = {1'b0,corr_sum5[4]};
	endgenerate
	
	// Seventh / Last rank & register
	reg [7:0] corr_val, corr_val_d;
	reg signed [8:0] corr_slope;
	always @(posedge clock)
		if(reset)
		begin
			corr_val <= 8'd0;
			corr_val_d <= 8'd0;
			corr_slope <= 9'd0;
		end
		else
		begin
			corr_val <= {1'b0,corr_sum6[2]} + {1'b0,corr_sum6[1]} + {1'b0,corr_sum6[0]};

			if(ena)
			begin
				corr_val_d <= corr_val;
				corr_slope <= $signed({1'b0,corr_val})-$signed({1'b0,corr_val_d});
			end
		end

	// Peak detect
	reg [3:0] window_cnt;
	reg [1:0] corr_state;
	reg data_start_z;
	always @(posedge clock)
		if(reset)
		begin
			window_cnt <= 4'd0;
			corr_state <= 2'b00;
			data_start_z <= 1'b0;
		end
		else if(ena)
			case(corr_state)
				2'b00:
					if(corr_val > CORR_THRESH)
					begin
						window_cnt <= 4'd10;
						corr_state <= 2'b01;
					end
				
				2'b01:
					begin
						if(trigger)
						begin
							window_cnt <= 4'd0;
							corr_state <= 2'b00;
						end
						else if(corr_slope[8] == 1'b1)
						begin
							corr_state <= 2'b10;
							data_start_z <= 1'b1;
						end
						window_cnt <= window_cnt - 1;
					end
					
				2'b10:
					begin
						if(trigger)
						begin
							window_cnt <= 4'd0;
							corr_state <= 2'b00;
						end
						else 
						begin
							data_start_z <= 1'b0;
							if(window_cnt == 4'd0)
								corr_state <= 2'b11;
							window_cnt <= window_cnt - 1;
						end
					end
				
				2'b11:
					if(trigger)
					begin
						window_cnt <= 4'd0;
						corr_state <= 2'b00;
					end
				
				default:
					begin
						corr_state <= 2'b00;
						window_cnt <= 2'b00;
					end
			endcase
	assign data_start = data_start_z;
			
	// align inputs with start signal
	reg [width:0] delay_pipe[0:PIPE_LENGTH-1];
	wire slice_data_d;
	wire [width-1:0] logmag_d;
	always @(posedge clock)
		if(reset)
			delay_pipe[0] <= 11'd0;
		else if(ena)
			delay_pipe[0] <= {slice_data,logmag};
	generate
		genvar gU;
		for(gU=0;gU<PIPE_LENGTH-1;gU=gU+1)
		begin:delay_pipe_blk
			always @(posedge clock)
				if(reset)
					delay_pipe[gU+1] <= 11'd0;
				else if(ena)
					delay_pipe[gU+1] <= delay_pipe[gU];
		end
	endgenerate
	assign slice_data_d = delay_pipe[PIPE_LENGTH-1][width];
	assign logmag_d = delay_pipe[PIPE_LENGTH-1][width-1:0];
	
	// sample timer and bit decision
	reg [4:0] sample_timer;
	reg sample;
	reg [3:0] sample_d;
	reg id_sgn;
	reg [width-1:0] bit0, bit1;
	reg raw_bit;
	always @(posedge clock)
	begin
		if(reset)
		begin
			sample_timer <= 5'd0;
			sample <= 1'b0;
			sample_d <= 4'b0;
			id_sgn <= 1'b0;
			bit0 <= {width{1'b0}};
			bit1 <= {width{1'b0}};
			raw_bit <= 1'b0;
		end
		else if(ena)
		begin
			if((data_start) | (sample_timer == (2*OVERSAMPLE-1)))
			begin
				sample_timer <= 5'd0;
				sample <= 1'b1;
				id_sgn <= 1'b0;
			end
			else
			begin
				sample_timer <= sample_timer + 1;
				sample <= 1'b0;
				if (sample_timer == Smpl1Time)
					bit1 <= logmag_d;

				if (sample_timer < (OVERSAMPLE-1))
					id_sgn <= 1'b0;
				else
					id_sgn <= 1'b1;
				
				if (sample_timer == Smpl0Time)
				begin
					bit0 <= logmag_d;
					if (bit1 > logmag_d)
						raw_bit <= 1'b1;
					else
						raw_bit <= 1'b0;
				end
			end
			sample_d <= {sample_d[2:0],sample};
		end
	end
	
	// Integrator and confidence decision
	reg signed [5:0] sgn_bits;
	wire [4:0] unsgn_bits = {4'd0,slice_data_d};
	always @(id_sgn or slice_data_d)
		if(id_sgn ^ slice_data_d)
			sgn_bits <= $signed(6'd1);
		else
			sgn_bits <= $signed(6'd63);
		
	reg signed [5:0] accum;
	reg signed [5:0] int_dump;
	reg [4:0] energy;
	reg [4:0] nrg_hold;
	reg valid_bit;
	reg empty_bit;
	always @(posedge clock)
	begin
		if(reset)
		begin
			accum <= 6'd0;
			int_dump <= 6'd0;
			energy <= 5'd0;
			nrg_hold <= 5'd0;
			valid_bit <= 1'b0;
			empty_bit <= 1'b0;
		end
		else if(ena)
		begin
			if(sample)
			begin
				int_dump <= accum;
				accum <= sgn_bits;
				
				if((absolute(accum) >= BIT_THRESH) & (~accum[5] == raw_bit))
					valid_bit <= 1'b1;
				else
					valid_bit <= 1'b0;
				
				nrg_hold <= energy;
				energy <= unsgn_bits;
				
				if(energy < 2)
					empty_bit <= 1'b1;
				else
					empty_bit <= 1'b0;
			end
			else
			begin
				accum <= accum + sgn_bits;
				energy <= energy + unsgn_bits;
			end
		end
	end
	
	// enable pipeline
	reg [3:0] ena_pipe;
	always @(posedge clock)
		if(reset)
			ena_pipe <= 4'd0;
		else
			ena_pipe <= {ena_pipe[2:0],ena};

	// Packet state machine
	reg [1:0] pkt_state;
	reg done_z;
	always @(posedge clock)
	begin
		if(reset)
		begin
			pkt_state <= 2'b00;
			done_z <= 1'b0;
		end
		else if(ena_pipe[3])
			case(pkt_state)
				2'b00:
					begin
						done_z <= 1'b0;
						if(data_start)
							pkt_state <= 2'b01;
					end
					
				2'b01:
					if(sample)
						pkt_state <= 2'b10;
					
				2'b10:
					if(sample)
						pkt_state <= 2'b11;
					
				2'b11:
					if (sample_d[0])
						if (empty_bit)
						begin
							done_z <= 1'b1;
							pkt_state <= 2'b00;
						end
						
				default:
					pkt_state <= 2'b00;
			endcase
	end
		
	// Need timer machine to generate desense recover
	// 1) when done at end of valid packet
	// 2) when no correlation seen after 8us
	// 3) others?
	
	assign ena_out = (sample_d[2] & ena_pipe[3] & (pkt_state == 2'b11));
	assign data = raw_bit;
	assign conf = valid_bit;
	assign done = done_z;
endmodule
