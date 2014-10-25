// adsb_rx.v - ads-b receiver
// 8-4-14 E. Brombaugh

module adsb_rx
	#(
		parameter width = 10
	)
	(
		input clock,						// main system clock
		input reset,						// main system reset
		input ena,							// enable input
		input det_ena,						// detect enable
		input noise_ride,					// enable noise riding MTL
		input [width-1:0] mtl,				// Min Thresh Level
		input [width-1:0] logmag,			// log magnitude data
		output trigger,						// P1 trigger output
		output data_start,					// preamble found output
		output ena_out,						// bitrate enable output
		output data,							// bit data output
		output conf,							// confidence output
		output done,							// frame done output
		output watchdog_reset,				// clear downstream
		output [width-1:0] thresh,			// threshold level output
		output [width-1:0] noise_out,		// noise estimate output
		output slice_data,					// sliced data
		output retrigger_out					// retrigger pulse
	);
	
	// Instantiate the Pulse Detector
	wire [width-1:0] logmag_dly;
	pulsedet #(.width(width))
		uPulseDet(.clock(clock), .reset(reset), .ena(ena),
			.trigger_clr(watchdog_reset), 
			.det_ena(det_ena), .noise_ride(noise_ride),
			.mtl(mtl), .logmag(logmag),
			.logmag_dly(logmag_dly), .trigger(trigger),
			.slice_data(slice_data), .thresh_out(thresh),
			.noise_out(noise_out), .retrigger_out(retrigger_out)
		);

	// Instantiate the PPM Demod
	ppm_demod #(.width(width))
		uPPMDemod(.clock(clock), .reset(watchdog_reset), .ena(ena),
			.logmag(logmag_dly),.trigger(trigger), .slice_data(slice_data),
			.data_start(data_start),
			.ena_out(ena_out), .data(data), .conf(conf), .done(done)
		);
		
	// Watchdog timer keeps track and resets
	watchdog
	 uWatchDog(.clock(clock), .reset(reset), .ena(ena), .det_ena(det_ena),
			.trigger(trigger), .data_start(data_start), .done(done),
			.watchdog_reset(watchdog_reset)
	);
endmodule
