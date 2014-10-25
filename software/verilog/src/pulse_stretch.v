// stretch.v - stretch input pulse by N clocks as visible diagnostic
// 2009-01-15 E. Brombaugh

module pulse_stretch(clk, reset, period, in, out);
	parameter bits = 32;			// bits in divider - must accept period
	
	input clk;					// Clock input to check
	input reset;				// POR
	input [bits-1:0] period;	// Count Period
	input in;					// short input signal
	output out;					// stretched output signal
	
	// loadable counter
	reg [bits-1:0] cntr;
	reg out_z;
	always @(posedge clk)
		if(reset)
		begin
			cntr <= 1'b0;
			out_z <= 1'b0;
		end
		else
		begin
			if(in)
			begin
				cntr <= period - 1;
				out_z <= 1'b1;
			end
			else if(cntr != 0)
				cntr <= cntr - 1 ;
			else
				out_z <= 1'b0;
		end
	
	// hook up output
	assign out = out_z;
endmodule
