#include "rtlsdr_skyline.h"
#include "logger.h"

// just a stupid global aid for the logger thing
static rtlsdr_skyline* my_main_window = NULL;
static void skyline_log_this(const char* msg)
{
	my_main_window->member_log_this(msg);
}


/* ### Main window constructor ### */

rtlsdr_skyline::rtlsdr_skyline() :
	buffer(NULL),
	from_freq_rounded(0), to_freq_rounded(0),
	display_in_calib_mode(false)
{
	// logger setup
	my_main_window = this; // could be done in GUI_APP_MAIN() way down below, but ahh well
	set_logger_callback(skyline_log_this);
	
	CtrlLayout(*this, "RTL-SDR Skyline - an overview spectrograph");
	Sizeable().Zoomable();
	Icon(ThumbImg::Thumb());
	//Button_close <<= THISBACK(button_close_click);
	Button_rescan <<= THISBACK(button_rescan_click);
	Button_go <<= THISBACK(button_go_click);
	Button_calib <<= THISBACK(button_calib_click);
	WhenClose = THISBACK(on_close); // WhenClose is an overridable callback of TopWindow
	//DropList_devices.WhenAction = ... Or should we hook WhenPush ? See the next line:
	DropList_devices <<=THISBACK(engage_device); // hooks WhenAction
	DropList_gains <<=THISBACK(gain_selected);
	Edit_from.WhenLostFocus << THISBACK(edit_from_action);
	Edit_to.WhenLostFocus << THISBACK(edit_to_action);
	DropList_win_size << THISBACK(droplist_win_size_action);
	DropList_rate << THISBACK(droplist_rate_action);
	
	// We probably should not mess with the window size and the number of windows while running,
	// so no callback for the relevant DropLists... but we'd better enter some entries for a
	// start:
	DropList_win_size.Add(4096);
	DropList_win_size.Add(2048);
	DropList_win_size.Add(1024);
	DropList_win_size.Add(512);
	DropList_win_size.Add(256);
	DropList_win_size.Add(128);
	//DropList_win_size.Add(64); // below 128, the truncation to a decimal-codivisible lot size
	//DropList_win_size.Add(32); //  is not possible. (Numer of frequency samples.)
	DropList_win_size.Activate();
	DropList_win_size.SetIndex(DropList_win_size.Find(2048));
	
	DropList_num_win.Add(1024);
	DropList_num_win.Add(256);
	DropList_num_win.Add(64);
	DropList_num_win.Add(32);
	DropList_num_win.Add(16);
	DropList_num_win.Add(8);
	DropList_num_win.Activate();
	DropList_num_win.SetIndex(DropList_num_win.Find(16));
	
	DropList_rate.Add(2048000);
	DropList_rate.Add(1024000);
	DropList_rate.Add( 256000);
	DropList_rate.Activate();
	DropList_rate.SetIndex(DropList_rate.Find(2048000));
	
	Edit_calib.SetData(1575.42); // the GPS carrier is about 30 dB below the thermal noise
	Option_calib.SetData(false);
	Option_calib.Hide();
	Option_loop.SetData(false);
	
	// The two seem to be equivalent:
	Edit_log.SetEditable(false);
	Edit_log.SetReadOnly();
	// Hide the faint underscore on the last line
	Edit_log.NoEofLine();
	
	blank_out_chart_grid(); // start with the chart as blank as possible
	Button_calib.Hide();
	
	rescan_devices();
}




/* ### RTL related stuff ### */

// Look for some Realtek dongles and preload the DropLists accordingly.
// Calls engage_device() internally and will leave the rtlsdr_dev #0 open,
// if no previous device was open already
void rtlsdr_skyline::rescan_devices()
{
 int dev_count;
 
	log_this("Rescanning devices...");

	dev_count = backend.rescan_devices();
	String my_msg = "Device count: ";
	my_msg << dev_count;
	log_this(my_msg);
	
	if (dev_count == 0)
	{
		log_this("No devices found?");
		DropList_devices.Clear();
		DropList_devices.Deactivate();
		clear_gains();
		//dev_idx_currently_engaged = -1;
		// would also rtlsdr_close(), except that by now, the device is probably gone anyway
	}
	else
	{
		// Start by resetting the DropList to "empty, active"
		DropList_devices.Clear();
		DropList_devices.Activate();
		
		for (int i=0; i<dev_count; i++)
		{
			String* entry_descr;
			entry_descr = backend.get_long_dev_descr(i);
			if (entry_descr != NULL)
				DropList_devices.Add(i,*entry_descr);
			else
				log_this("Error obtaining long description for this device.");
		}
		
		int curr_dev = backend.get_current_device(); // just an index in the list
		if (curr_dev >= 0)
		{
			DropList_devices.SetIndex(curr_dev);
		}
		else
		{
			//(should we better let the user select a device from the listbox?)
			DropList_devices.SetIndex(0); // This alone doesn't trigger WhenAction of the DropList
		}

		if (dev_count == 1) // exactly one device - just load its gains to save clicks
		{
			log_this("Exactly one device found.");
			engage_device();
		}
		else
			log_this("Devices found, please select one from the list.");
	}
}



// Engages device based on the DropList's selected entry.
// Called by rescan_devices and also implicitly by the DropList_devices' WhenAction callback
void rtlsdr_skyline::engage_device()
{
	int dev_idx = DropList_devices.GetIndex();
	if (dev_idx < 0) return;
	backend.set_current_device(dev_idx);

	//PromptOK("Opening device...");
	log_this("Opening device...");
	if (backend.open() >= 0)
	{
		rescan_gains();
	}
	else
	{
		log_this("Failed to open the back end?");
		clear_gains();
	}
	
	invalidate_calibration();

	return;
}



// Fill DropList_gains. Needs to have the RTL-SDR device already open.
// The DropList_gains visual widget shares an index with the "gains" array (now hidden in the
// backend)
void rtlsdr_skyline::rescan_gains()
{
	// Note: clear_gains() here refers to rtlsdr_skyline::clear_gains(), not to the backend
	clear_gains();
 
	log_this("Rescanning gains...");
	int num_gains = backend.rescan_gains();
	if (num_gains > 0)
	{
		DropList_gains.Activate();

		for (int i=0; i < num_gains; i++)
		{
			//log_this("Adding gain...");
			String gain_descr;
			gain_descr << (((double)backend.get_gain(i))/10) << " dB";
			DropList_gains.Add(i,gain_descr);
		}
		DropList_gains.SetIndex(0); // TODO - maybe return to the gain previously set?

		log_this("Gains rescanned and entry #0 selected.");
		
		set_gain();
	}
	else if (num_gains < 0)
		log_this("Error getting gains ?");
	else
		log_this("This device has no gains ?");
}


// Flush the DropList_gains. Doesn't call the backend.
void rtlsdr_skyline::clear_gains()
{
	DropList_gains.Clear();
	DropList_gains.Deactivate();
}


// Configure the gain parameters in the RTL-SDR dongle
int rtlsdr_skyline::set_gain()
{
 int retval = 0;
 
	if (DropList_gains.GetIndex() >= 0)
	{
		backend.gain_idx = DropList_gains.GetIndex();
		retval = backend.set_gain(); // really somewhat superfluous, but we want a response

		switch(retval)
		{
			case  0: log_this("Backend: Gain configured."); break;
			case -1: log_this("Backend: dev not open"); break;
			case -2: log_this("Backend: gains not inited"); break;
			case -3: log_this("Backend: failed to set manual gain mode"); break;
			case -4: log_this("Backend: failed to set gain level"); break;
			case -5: log_this("Backend: failed to disable RTL AGC"); break;
			default: log_this("Backend: unknown error"); break;
		}
	}
	else
	{
		log_this("No gain selected.");
		retval = -10;
	}

	return retval;
}


int rtlsdr_skyline::set_samp_rate(int samp_rate)
{
 int retval = 0;

	backend.samp_rate = samp_rate;
	retval = backend.set_samp_rate(); // somewhat superfluous, but we want a response
	
	if (retval)
		log_this("Failed to set sampling rate.");
	else
		log_this("Sampling rate configured.");
	
	return retval;
}



// ### some helpers ###

void rtlsdr_skyline::member_log_this(const char* log_msg)
{
	log_text = "\n";
	log_text << log_msg;
	Edit_log.Append(log_text);
	scroll_log();
}


void rtlsdr_skyline::scroll_log()
{
	Edit_log.SetCursor(Edit_log.GetLength()); // scroll down to the last line (character)
}


void rtlsdr_skyline::on_close()
{
	//backend.close(); // not necessary, does the cleanup in a destructor
	//backend.cleanup(); // not necessary, does the cleanup in a destructor
	//stop any activities going on // continue here - this needs to be updated
	Chart1.RemoveAllSeries();
	if (buffer != NULL) delete buffer;
	Close();
}



// The following two timing helpers are intended just for a bit of profiling.
// Apparently, U++ doesn't have this solved in a platform-agnostic fashion,
// so we need to use OS-specific timing stuff (this is for Windows)
// We don't expect to run a profiling test across midnight.

static int systime_to_msec(SYSTEMTIME* in)
{
	return ( in->wHour * 3600000 + in->wMinute * 60000
					+ in->wSecond * 1000 + in->wMilliseconds );
}

static int time_diff_in_msec(SYSTEMTIME* time_from, SYSTEMTIME* time_to)
{
	int ms_from = systime_to_msec(time_from);
	int ms_to = systime_to_msec(time_to);
	return ms_to - ms_from;
}



/*

Sampling and FFT bandwidth math:

The frequency-domain bandwidth coming out of FFT depends on the sampling rate.
The FFT of a QAM-demodulated baseband signal produces +/- Nyquist frequency
(centered around the instantaneous tuned frequency).
I.e., the raw bandwidth in the spectrum image is equal to the sampling rate!
(Apparently this is only true on the condition, that the FFT's input signal
consists of complex=quadrature samples, so that negative frequencies make sense,
so that the negative "DC to -Nyqyuist" half-spectrum contains information.)

The window size (number of samples per FFT window) does not affect the frequency bandwidth
around a particular tuned frequency - rather, it translates into the number of discrete bands
(data points) in the frequency domain, per bandwidth that is given by the sampling freq.

Note that for FFT, the window size must be 2^n (the Fast Fourier Transform algorithm hinges
on that binary-aligned size). Therefore, the number of bands in the spectrum, per tuned
frequency, will also be 2^n. Therefore, if we want the basic discrete bands to be neatly
aligned to kHz or some such decimal quantum, we need the sampling frequency to involve 2^n.

Such as, for a discrete frequency-domain granularity of 1 kHz, and a window size of 2048
samples, we get a sampling frequency of 2048 kHz (kSps) - this is fairly close to the
empirically observed maximum of cca 2.4 MHz (MSps) for the RTL2832 dongles (before buffer
overflows/underflows happen in the data path). Roughly 2 MHz also appears to be the practical
analog bandwidth of the tuned channel: beyond +/- 1 MHz you can see a roll-off in the
calibration data (filtered own noise).

So far so good, seems like we can scan 2MHz bands, maybe with some spectrum-domain calibration.
And then, there's that pesky central spike in each channel.
DC removal doesn't help much, so we can as well just cut that out. This is done in the back
end...
*/


/* ### chart plotting stuff ### */

void rtlsdr_skyline::scan_band_crude()
{
	//Button_go.Deactivate();
	// these are actual band boundaries, not the frequencies to tune into.
	double from_freq = 0, to_freq = 0;
	// these are the frequencies that shall be tuned into:
	int tune_from = 0, tune_to = 0, single_freq = 0, initial_freq = 0, current_freq = 0;
	int samp_rate = 0, bandwidth = 0;
	int win_size = 0, num_win = 0, buf_size = 0; // display buffer size, not backend buf_size
	int bin_dec_offset = 0, tuning_offset = 0, dst = 0;
	int freq_bin_width = 0, win_size_decimal = 0;
	int num_freqs = 1; // number of frequencies to tune into, across the sweep requested
	String tmp_str = "";

	if (DropList_devices.GetIndex() < 0)
	{
		log_this("No device selected! Select one and try again.");
		goto over;
	}

	if (DropList_gains.GetIndex() < 0)
	{
		log_this("No gain selected! Select one and try again.");
		goto over;
	}

	if (IsNull(Edit_from))
	{
		log_this("Please enter a \"From\" frequency.");
		goto over;
	}
	
	if (IsNull(Edit_to))
	{
		log_this("Please enter a \"To\" frequency.");
		goto over;
	}
	
	if ((double)Edit_from.GetData() < 0)
	{
		log_this("The From frequency must not be negative.");
		goto over;
	}

	if ((double)Edit_to.GetData() < 0)
	{
		log_this("The To frequency must not be negative.");
		goto over;
	}
	// Further range checking is perhaps better left up to live responses from the tuner dongle.
	
	from_freq = fround(Edit_from * 1000000);
	to_freq = fround(Edit_to * 1000000);

	if (from_freq > to_freq)
	{
		// actually From == To is also fine.
		log_this("Please make From < To. We scan in ascending order.");
		goto over;
	}
	
	// Clean up the ScatterCtrl object (visual chart) and the underlying storage.
	Chart1.RemoveAllSeries();
	//The ScatterCtrl Chart1 does not own its data. Now it's safe to delete the storage space.
	if (buffer != NULL) delete buffer;
	
	samp_rate = DropList_rate.GetValue();
	bandwidth = samp_rate_to_bandwidth(samp_rate);

	if (from_freq < to_freq)
	{
		double freq_diff = to_freq - from_freq;
		if (freq_diff < bandwidth)
			// handle the task using a single tuned freq.
			single_freq = (to_freq + from_freq) / 2;
		else // we need to tune into multiple frequencies consecutively
		{
			// We'd better re-align the boundaries of the range requested
			// to integer multiples of the beautified decimal bandwidth...
			from_freq_rounded = ffloor(from_freq / bandwidth) * bandwidth;
			to_freq_rounded = fceil(to_freq / bandwidth) * bandwidth;
			tune_from = ffloor(from_freq_rounded + bandwidth/2);
			tune_to = ffloor(to_freq_rounded - bandwidth/2);
			num_freqs = (tune_to - tune_from) / bandwidth + 1;
		}
	}
	else // From and To are equal = scan just one channel
		single_freq = from_freq; // == to_freq

	if (single_freq != 0)
	{
		from_freq_rounded = single_freq - bandwidth/2;
		to_freq_rounded = single_freq + bandwidth/2;
	}

	log_this("Configuring the backend...");
	if (set_gain())	goto over;
	if (set_samp_rate(samp_rate)) goto over;

    win_size = DropList_win_size.GetValue();
    num_win = DropList_num_win.GetValue();
	backend.set_buf_size(win_size, num_win);
	backend.apply_calib = Option_calib.GetData();
	
	// Note: in the visual buffer, we only need "bandwidth" worth of data points (samples),
	// rather than "window size" worth. And, it is useful to truncate the binary-sized
	// frequency domain image to a "decimal" channel width (number of frequency samples).
	// The ratio will be 1.024, resulting in an integer number of samples
	// for FFT window sizes down to 128. Smaller FFT window sizes might be some use,
	// as for broad overview sweeps we don't need all the frequency resolution anyway -
	// - but would not result in an integer number of frequency samples per slot in the decimal
	// frequency raster :-(  Perhaps I should stick to a binary raster, and truncate for display
	// afterwards?
	// Anyway while sticking to the decimal frequency raster, we need to limit the selection
	// of window sizes to 128 samples minimum. Note that at 128 FFT samples, the resulting
	// "decimated" channel (125 frequency samples) is already odd = centered around the middle
	// of a particular frequency sample, rather than around a sample boundary.
	freq_bin_width = samp_rate / win_size;
	win_size_decimal = bandwidth / freq_bin_width;
	buf_size = win_size_decimal * num_freqs;
	buffer = new double[buf_size];
	for (int i=0; i<buf_size; i++)
		buffer[i] = 0;

	// tweak the binary to decimal buffer copy offset (and optionally tuning offset)
	if (win_size == 128)
	{
		bin_dec_offset = 2;
		tuning_offset = (-1) * freq_bin_width / 2;
	}
	else
		bin_dec_offset = (win_size - win_size_decimal) / 2;

	// gonna be sweeping or not?
	if (single_freq != 0) // we'll scan just a single freq (no sweep)
		// we could actually show the binary-sized window of frequency samples, but ahh well...
		// We'd have to re-do all the math around here, alloc the buffer differently
		// and whatnot. => Let's stick to the decimal bandwidth.
		initial_freq = single_freq;
	else // we'll sweep across frequencies
		initial_freq = tune_from;

	log_this("Adding buffer to the chart");
	Chart1.AddSeries(buffer, buf_size,
					/*x0*/ from_freq_rounded / 1000000, // Hz
					/*delta x*/ (double)freq_bin_width / (double)1000000// HZ
					);
	//Chart1.ZoomToFit(true,true,0); // X, Y, fill factor (0 = fill the whole control)
	display_in_calib_mode = false;
	set_chart_grid();
	Chart1.NoMark(0);
	Chart1.Stroke(0,1,Blue());
	Chart1.Refresh();
	Ctrl::ProcessEvents();

	backend.alloc();

    do {
        current_freq = initial_freq;
        
		// now finally iterate across the swept spectrum (or don't sweep at all if num_freqs == 1)
		for (int i=0, dst=0; i < num_freqs; i++)
		{
			SYSTEMTIME time1, time2, time3; // profiling
			
			// Note: the tuning offset here is NOT the tuning offset as in "offset tuning",
			// a hardware capability of the RTL2832...
			// Rather, it's just a bit of an alignment aid for window size 128 samples.
			// When cropped to a "decimal" 125 samples, the number of frequency quanta is odd,
			// meaning that we cannot crop symmetrically an integer count of frequency quanta
			// off each end of the channel bandwidth, meaning that we need to offset the tune
			// by half a frequency quantum to get it aligned consistently to the even-numbered
			// spectral windows. (Aligned to the range of from_freq<-->to_freq, which is centered
			// in terms of the low-level binary bandwidth and therefore has an even count of freq.lines.)
			backend.freq = current_freq + tuning_offset;
			  GetLocalTime(&time1);
			backend.scan_one_freq();
			  GetLocalTime(&time2);
			backend.crunch_data();
			  GetLocalTime(&time3);
			
			tmp_str = "";
			tmp_str << "scanning: " << time_diff_in_msec(&time1, &time2)
					<< " ms, crunching: " << time_diff_in_msec(&time2, &time3);
			log_this(tmp_str);
			
			for (int src = bin_dec_offset;  src < win_size_decimal + bin_dec_offset;  src++,dst++)
			{ // note: dst keeps rolling across iterations of "i"
				buffer[dst] = backend.avgs[src];
			}
			
			Chart1.Refresh();
			Ctrl::ProcessEvents();
			
			current_freq += bandwidth;
		}
    } while (Option_loop.GetData() == true);

	backend.cleanup();
					
over:
	//Button_go.Activate();
	return;
}



// Only valid for the current device at the current FFT size and sampling rate.
void rtlsdr_skyline::calibrate()
{
	int single_freq = 1575420000; // GPS - there should be just thermal noise in that band
	int samp_rate = 0;
	int win_size = 0, num_win = 0, buf_size = 0; // display buffer size, not backend buf_size
	double freq_bin_width = 0;

	if (DropList_devices.GetIndex() < 0)
	{
		log_this("No device selected! Select one and try again.");
		goto over;
	}

	if (IsNull(Edit_calib))
	{
		log_this("Will calibrate at the default freq: 1575.42 MHz.");
		goto over;
	}
	
	if ((double)Edit_calib.GetData() < 0)
	{
		log_this("Invalid calibration frequency. Resetting to default.");
		Edit_calib.SetData(1575.42);
		goto over;
	}

	single_freq = (double)Edit_calib.GetData() * 1000000;

	if (backend.get_num_gains_in_cur_dev() == 0)
		backend.rescan_gains();
	backend.gain_idx = 0;  // this tends to be the lowest gain, which is what we want
	//backend.set_gain();  // the first thing done by scan_one_freq();
	
	// Clean up the ScatterCtrl object (visual chart) and the underlying storage.
	Chart1.RemoveAllSeries();
	//The ScatterCtrl Chart1 does not own its data. Now it's safe to delete the storage space.
	if (buffer != NULL) delete buffer;
	
	samp_rate = DropList_rate.GetValue();

	log_this("Configuring the backend...");
	if (set_gain())	goto over;
	if (set_samp_rate(samp_rate)) goto over;

	// Note: in scan_band_crude, this math is different(ish)
    win_size = DropList_win_size.GetValue();
    num_win = 1024; // to have a large set to average across
	backend.set_buf_size(win_size, num_win);
	freq_bin_width = samp_rate / win_size;
	buf_size = win_size; // visual buffer - just one window for the averaged calibration data
	buffer = new double[buf_size];
	
	for (int i=0; i<buf_size; i++)
		buffer[i] = 0;

	log_this("Adding buffer to the chart");
	Chart1.AddSeries(buffer, buf_size,
					/*x0*/ (-1)* (double)samp_rate / 2 / 1000000, // Hz
					/*delta x*/ freq_bin_width / (double)1000000// HZ
					);
					
	//Chart1.ZoomToFit(true,true,0); // X, Y, fill factor (0 = fill the whole control)
	Chart1.SetXYMin( (-1)* (double)samp_rate / 2 / 1000000, -45);
	Chart1.SetRange((double)samp_rate / 1000000, 90);
	// we have a function to round the binary sampling rate
	// to the nearest smaller decimal bandwidth:
	Chart1.SetMajorUnits(1, 10);

	Chart1.SetMinUnits(((double)samp_rate - samp_rate_to_bandwidth(samp_rate))/2000000,5);
	
	Chart1.SetLabels("MHz","dBFS");
	Chart1.ShowLegend(false);

	Chart1.NoMark(0);
	Chart1.Stroke(0,1,Blue());
	Chart1.Refresh();
	Ctrl::ProcessEvents();
	display_in_calib_mode = true;

	// The calibration buffer has a different lifetime, and is therefore allocated
	// independently from the other backend buffers.
	// And, it's time to (re)allocate.
	backend.invalidate_calibration();
	backend.alloc_calib();
	
	backend.alloc(); // the several crunching buffers

	backend.freq = single_freq;
	backend.scan_one_freq();
	backend.crunch_data(/*calibrate?*/ true);

	for (int sample = 0; sample < win_size; sample++)
	{
		buffer[sample] = backend.avgs[sample];
	}
	
	Chart1.Refresh();
	Ctrl::ProcessEvents();

	backend.cleanup(); // Note: the calibration data lives on at this point.
	
	//Button_calib.Deactivate();
	Button_calib.Hide();
	Option_calib.Show();
over:
	return;
}



void rtlsdr_skyline::invalidate_calibration()
{
	backend.invalidate_calibration();
	//Button_calib.Activate();
	Button_calib.Show();
	Option_calib.Hide();
}



/* ### GUI event handlers ### */

/*
void rtlsdr_skyline::button_close_click()
{
	on_close();
}
*/

void rtlsdr_skyline::button_rescan_click()
{
	rescan_devices();
	invalidate_calibration();
}


void rtlsdr_skyline::button_go_click()
{
	scan_band_crude();
}


void rtlsdr_skyline::button_calib_click()
{
	calibrate();
}


void rtlsdr_skyline::gain_selected()
{
	log_this("Gain selected.");
}


void rtlsdr_skyline::edit_from_action()
{
	if ((IsNull(Edit_to)) || (Edit_to.GetData() < Edit_from.GetData()))
		Edit_to.SetData(Edit_from.GetData());
	//else do not touch edit_to
}


void rtlsdr_skyline::edit_to_action()
{
	if ((IsNull(Edit_from)) || (Edit_from.GetData() > Edit_to.GetData()))
		Edit_from.SetData(Edit_to.GetData());
	//else do not touch edit_from
}


void rtlsdr_skyline::droplist_win_size_action()
{
	invalidate_calibration();
}


void rtlsdr_skyline::droplist_rate_action()
{
	invalidate_calibration();
}




// Minimum spacing between grid ticks, along the horizontal axis, in pixels.
// The MHz labels have to fit in - there's a label per every grid tick.
#define DIV_NO_SMALLER_THAN_PX 50

// the returned value is in MHz
double rtlsdr_skyline::chart1_optimal_major_unit_x()
{
	double retval = 1; // default response (1 MHz)
	double decimal_order = 0;
	int chart_horiz_pixels = Chart1.GetPlotWidth();

	if (chart_horiz_pixels < DIV_NO_SMALLER_THAN_PX)
		return retval; // prevent dv_by_zero. Return the default response.
	
	// we could also use "bandwidth" and "num_freqs" and whatnot...
	// but from_freq_rounded and to_freq_rounded are perhaps the closest to the visual chart.
	double chart_bandwidth = (to_freq_rounded - from_freq_rounded) / 1000000;
	double mhz_per_optimal_div = chart_bandwidth / (chart_horiz_pixels / DIV_NO_SMALLER_THAN_PX);

	for (decimal_order = 0.001; decimal_order <= 1000; decimal_order *= 10)
	{
		if (decimal_order > mhz_per_optimal_div)
		{
			retval = decimal_order;
			break;
		}
		else if (decimal_order * 2 > mhz_per_optimal_div)
		{
			retval = decimal_order * 2;
			break;
		}
		else if (decimal_order * 5 > mhz_per_optimal_div)
		{
			retval = decimal_order * 5;
			break;
		}
		// else try another decimal order
	}
	
	return retval;
}



void rtlsdr_skyline::blank_out_chart_grid()
{
	Chart1.SetXYMin(0,0);
	Chart1.SetRange(1,1);
	Chart1.SetMinUnits(0,0);
	Chart1.SetMajorUnits(1, 1);
	
	Chart1.SetLabels("","");
}



void rtlsdr_skyline::set_chart_grid()
{
	if (display_in_calib_mode) return; // no modification needed
	if ((from_freq_rounded == 0) || (to_freq_rounded == 0)) return; // it's too early
	
	// grid spacing along axis X
	double x_tick = chart1_optimal_major_unit_x();
	
	// avoid unnecessary fractional digits in grid tick label,
	// by shifting the grid conveniently into alignment with integer multiples of grid tick
	double x_ofs = (from_freq_rounded/1000000) - (ffloor(from_freq_rounded / 1000000 / x_tick) * x_tick);
	
	Chart1.SetXYMin(from_freq_rounded / 1000000, -100);
	Chart1.SetRange((to_freq_rounded - from_freq_rounded)/ 1000000, 100); // auto-adjust dB?
	Chart1.SetMinUnits(x_ofs,0);
	Chart1.SetMajorUnits(x_tick, 10);
	
	Chart1.SetLabels("MHz","dBFS");
	Chart1.ShowLegend(false);
}



/* ### App main() ### */

GUI_APP_MAIN
{
	rtlsdr_skyline().Run();
}
