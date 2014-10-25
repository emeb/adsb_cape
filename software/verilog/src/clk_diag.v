// clk_diag.v - divide clock input by N as visible diagnostic
// 2009-01-15 E. Brombaugh

module clk_diag(clk, reset, period, tc, out);
	parameter bits = 32;			// bits in divider - must accept period
	
	input clk;					// Clock input to check
	input reset;				// POR
	input [bits-1:0] period;	// Count Period
	output tc;					// terminal count
	output out;					// diagnostic flash output
	
	// main divider
	reg [bits-1:0] cntr;
	always @(posedge clk)
		if(reset)
			cntr <= period;
		else
		begin
			if(cntr == 0)
				cntr <= period - 1;
			else
				cntr <= cntr - 1 ;
		end
	
	// terminal count detector
	reg tc;
	always @(posedge clk)
		if(reset)
			tc <= 1'b0;
		else
			tc <= (cntr == 0);
		
	// divide by 2 for visibility
	reg out;
	always @(posedge clk)
		if(reset)
			out <= 0;
		else
		begin
			if(tc)
				out <= ~out;
		end
endmodule
