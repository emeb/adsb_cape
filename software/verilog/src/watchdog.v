// watchdog.v - timer to reset adsb_rx when things lock up
// 8-4-14 E. Brombaugh

module watchdog
	(
		input clock,						// main system clock
		input reset,						// main system reset
		input ena,							// enable input
		input det_ena,						// detect enable
		input trigger,						// P1 leading edge
		input data_start,					// preamble seen / start of data
		input done,							// end of frame
		output watchdog_reset				// reset logic for next
	);
	
	// main state machine
	reg [1:0] wd_state;
	reg [11:0] wd_timer;
	reg timeout;
	always @(posedge clock)
		if(reset)
		begin
			wd_state <= 2'b00;
			wd_timer <= 12'd0;
			timeout <= 1'b1;
		end
		else if(ena)
		begin
			case(wd_state)
				2'b00:	// Waiting for trigger
				begin
					timeout <= 1'b0;	// clear the previous timeout
					if(det_ena & trigger)
					begin
						// Triggered - advance to wait for data start
						wd_state <= 2'b01;
					end
				end
				
				2'b01:	// Wait 9us for data_start
				begin
					if(!det_ena)
					begin
						// Detect disabled while waiting
						// issue reset and go to start 
						wd_state <= 2'b00;
						wd_timer <= 12'd0;
						timeout <= 1'b1;
					end
					else if(trigger)
					begin
						// Retriggered - reset timer and wait for data start
						wd_timer <= 12'd0;
					end
					else
					begin
						// still waiting - advance timer
						wd_timer <= wd_timer + 12'd1;
						
						// timer expired?
						if(wd_timer > 12'd180)
						begin
							// yes - issue timeout and restart
							wd_state <= 2'b00;
							wd_timer <= 12'd0;
							timeout <= 1'b1;
						end
						else
						begin
							// no - test for data start
							if(data_start)
							begin
								// found data start - advance to wait for done
								wd_state <= 2'b10;
								wd_timer <= 12'd0;
							end
						end
					end
				end
				
				2'b10:	// Waiting 113us for done
				begin
					if(!det_ena)
					begin
						// Detect disabled while waiting
						// issue reset and go to start 
						wd_state <= 2'b00;
						wd_timer <= 12'd0;
						timeout <= 1'b1;
					end
					else if(trigger)
					begin
						// Retriggered - reset timer and wait for data start
						wd_timer <= 12'd0;
						wd_state <= 2'b01;
					end
					else
					begin
						// still waiting - advance timer
						wd_timer <= wd_timer + 12'd1;
						
						// timer expired or done?
						if((wd_timer > 12'd2270) | done)
						begin
							// found done - reset
							wd_state <= 2'b00;
							wd_timer <= 12'd0;
							timeout <= 1'b1;
						end
					end
				end
				
				default:	// all others go to known state
				begin
					// found done - reset
					wd_state <= 2'b00;
					wd_timer <= 12'd0;
					timeout <= 1'b1;
				end
			endcase
		end
	
	// reclock reset
	reg watchdog_reset_z;
	always @(posedge clock)
		if(reset)
			watchdog_reset_z <= 1'b1;
		else if(ena)
			watchdog_reset_z <= timeout;
	
	// hook up output
	assign watchdog_reset = watchdog_reset_z;
endmodule
