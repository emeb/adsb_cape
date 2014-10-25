// Eric Brombaugh
module usat(in, out);
   parameter isz = 13;  // input data width
   parameter osz = 12;  // output data width

   input [isz-1:0] in;   // input data to be saturated
   output [osz-1:0] out; // output data after saturation

   // Check high saturation condition
   wire max = |in[isz-1:osz];

   reg [osz-1:0] out;

   // select output
   always @(max or in)
		if(max)
			out = {osz{1'b1}};  // max
		else
			out = in[osz-1:0];  // pass thru
			
endmodule