// pulsedet.v - ads-b pulse detector
// 8-1-14 E. Brombaugh

module pulsedet
	#(
		parameter width = 10
	)
	(
		input clock,						// main system clock
		input reset,						// main system reset
		input ena,							// enable input
		input trigger_clr,				// clear trigger & reset thresh
		input det_ena,						// detect enable
		input noise_ride,					// enable noise riding MTL
		input [width-1:0] mtl,			// min threshold input
		input [width-1:0] logmag,		// log magnitude input
		output [width-1:0] logmag_dly,// aligned log magnitude output
		output trigger,					// trigger on leading edge
		output slice_data,				// sliced binary threshold data
		output [width-1:0] thresh_out,// threshold output
		output [width-1:0] noise_out,	// Noise floor estimate
		output retrigger_out				// retrigger pulse
	);
	
	// internal constants
	parameter noise_coef = 8;			// Noise IIR power of 2 coeff
	parameter [width-1:0] desense_amt = 60;		// thresh below P1
	parameter [width-1:0] desense_qual = 63;	// allow desense above MTL
	parameter [width-1:0] retrig_diff = 96;		// allow retrig above thresh
	parameter qual_len = 6;						// 300ns per DO260
	
	// IIR Noise Estimation
	reg [qual_len-1:0] thresh_pipe;
	reg trigger_state;
	reg [width+noise_coef-1:0] noise_est_full;
	wire [width-1:0] noise_est;
	always @(posedge clock)
	begin
		if(reset)
			noise_est_full <= {width+noise_coef{1'b0}};
		else if(ena & !trigger_state & ! thresh_pipe[0]) 
			noise_est_full <= noise_est_full + logmag - noise_est;
	end
	
	// Shift down & truncate noise_est for output and feedback
	assign noise_est = noise_est_full[width+noise_coef-1:noise_coef];
	
	// add noise estimate to MTL?
	reg [width:0] mtl_sum;
	wire [width-1:0] mtl_sat;
	reg [width-1:0] mtl_adj;
	always @(posedge clock)
	begin
		if(reset)
		begin
			mtl_sum <= {width{1'b0}};
			mtl_adj <= {width{1'b0}};
		end
		else
		begin
			mtl_sum <= {1'b0,mtl} + {1'b0,noise_est};
			if(noise_ride)
				mtl_adj <= mtl_sat;
			else
				mtl_adj <= mtl;
		end
	end
	
	// unsigned saturate mtl sum
	usat #(.isz(width+1), .osz(width))
		uusat(.in(mtl_sum), .out(mtl_sat));
		
	// Desense Qualified threshold
	reg [width-1:0] desense_qual_thresh;
	always @(posedge clock)
		desense_qual_thresh <= mtl_adj + desense_qual;
	
	// threshold comparator and pipeline
	reg [width-1:0] thresh;
	wire thresh_cross = (logmag > thresh) & (det_ena == 1'b1);
	wire slice_data_e = thresh_pipe[qual_len-1];
	always @(posedge clock)
		if(reset)
			thresh_pipe <= 10'h000;
		else if(ena)
			thresh_pipe <= {thresh_pipe[qual_len-2:0],thresh_cross};
	
	// check for initial trigger
	wire trigger_e = !trigger_state & (&thresh_pipe[qual_len-1:0]);
	reg [1:0] pk_dly;
	always @(posedge clock)
		if(reset)
		begin
			trigger_state <= 1'b0;
			pk_dly <= 2'b0;
		end
		else if(ena)
		begin
			if(trigger_state == 1'b0)
			begin
				// filtered detection of P1
				if(trigger_e)
				begin
					// trigger if rising edge and no previous trigger
					trigger_state <= 1'b1;
				end
			end
			else if(trigger_clr)
				trigger_state <= 1'b0;
				
			// delay trigger
			pk_dly <= {pk_dly[0],trigger_e};
		end
	
	// Threshold update
	wire retrigger_e;
	always @(posedge clock)
		if(reset)
			thresh <= mtl_adj;
		else if(ena)
		begin
			if((pk_dly[1] & (logmag > desense_qual_thresh)) | retrigger_e)
				thresh <= logmag - desense_amt;
			else
			begin
				if(!trigger_state | trigger_e | (|pk_dly))
					thresh <= mtl_adj;
			end
		end
	
	// retriggering
	wire raw_retrig = ((logmag > (thresh + retrig_diff)) & trigger_state);
	reg [qual_len-1:0] retrig_pipe;
	reg retrigger_state;
	assign retrigger_e = !retrigger_state & (&retrig_pipe);
	always @(posedge clock)
		if(reset)
		begin
			retrig_pipe <= {qual_len{1'b0}};
			retrigger_state <= 1'b0;
		end
		else if(ena)
		begin
			// retrigger pipeline
			retrig_pipe <= {retrig_pipe[qual_len-2:0],raw_retrig};
			
			// filtered detection
			if(!retrigger_state)
			begin
				if(retrigger_e)
					retrigger_state <= 1'b1;
			end
			else
			begin
				if(!(&retrig_pipe))
					retrigger_state <= 1'b0;
			end
		end
	
	// delay log magnitude to match
	reg signed [width-1:0] lm_dly[0:qual_len+1];

	// first element
	always @(posedge clock)
		if(reset)
			lm_dly[0] <= {width{1'b0}};
		else
			lm_dly[0] <= logmag;

	// rest of elements
	generate
		genvar i;
		for(i=0;i<qual_len+1;i=i+1)
		begin: lm_dly_blk
			always @(posedge clock)
				if(reset)
					lm_dly[i+1] <= {width{1'b0}};
				else if(ena)
					lm_dly[i+1] <= lm_dly[i];
		end
	endgenerate

	// register outputs
	reg trigger_z;
	reg slice_data_z;
	always @(posedge clock)
		if(reset)
		begin
			trigger_z <= 1'b0;
			slice_data_z <= 1'b0;
		end
		else if(ena)
		begin
			trigger_z <= trigger_e | retrigger_e;
			slice_data_z <= slice_data_e;
		end
		
	// assign outputs
	assign logmag_dly = lm_dly[qual_len+1];
	assign trigger = trigger_z;
	assign slice_data = slice_data_z;
	assign thresh_out = thresh;
	assign noise_out = noise_est;
	assign retrigger_out = retrigger_e;
endmodule
