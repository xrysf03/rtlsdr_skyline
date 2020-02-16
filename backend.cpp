#include "backend.h"
#include "kissfft/kiss_fft.h"
//#include <kiss_fft.h>
#include "logger.h"
//#include <strings.h> //bzero() - doesn't work, bzero is probably deprecated
//#include <string.h> //memset() - apparently the #include is not even needed...



/* Class rtlsdr_device */

// static member init
int rtlsdr_device::num_devs = 0;
int rtlsdr_device::current_dev = -1;
String rtlsdr_device::current_dev_serial = "";
ArrayMap<String,int> rtlsdr_device::by_serial;
Vector<rtlsdr_device*> rtlsdr_device::by_index;

//static
int rtlsdr_device::rescan_devices() // retval: number of devices found upon rescan
{
	empty_dev_list(); // checks if the list is non-empty first
	
	num_devs = get_device_count_from_driver();
	if (num_devs > 0)
	{
		// Re-populate the lists.
        // the rtlsdr library expects *us* to provide the temporary storage space. Ahh well.
		char* tmp_buf = new char[780];
		char* _manufact = tmp_buf;
		char* _product = &tmp_buf[260];
		char* _serial = &tmp_buf[520];
		
		for (int i=0; i < num_devs; i++)
		{
			rtlsdr_device* new_dev = new rtlsdr_device(i);
			new_dev->name = rtlsdr_get_device_name(i);
			rtlsdr_get_device_usb_strings(i, _manufact, _product, _serial);
			new_dev->manufact = _manufact;
			new_dev->product = _product;
			new_dev->serial = _serial;
			new_dev->long_description << "Dev " << i << ": " <<
				new_dev->name << " " <<
				new_dev->manufact << " " <<
				new_dev->product << " " <<
				new_dev->serial;
			by_index.Add(new_dev);
			by_serial.Add(new_dev->serial,i);
			
		}
		
		if (current_dev >= 0) // there was some device previously selected
		{
			if (current_dev_serial != "")
			{
				// try to find the previously known S/N among the newly scanned devices
				int previously_known = get_dev_idx_by_serial(current_dev_serial);
				if (previously_known >= 0)
				{
					current_dev = previously_known;
					// note: by now the previously known device has been closed gracefully.
					// Will have to be reopened. Probably by or before rescan_gains().
					// No need to be sorry about that. The GUI will have to sequence the steps.
				}
				else
					current_dev = -1;
					current_dev_serial = "";
			}
			else // no serial for the past current_device - is this an internal error?
			{
				if (current_dev >= num_devs) // out of range anyway => bail out
					current_dev = -1;
				// else let us assume that the index of the current device has remained the same
			}
		}
		// else there was no device previously selected
		
		delete tmp_buf;
	}
	else
	{
		current_dev = -1;
		current_dev_serial = "";
	}
	
	return num_devs;
}



//static
String* rtlsdr_device::get_device_name(int index)
{
	if (index < num_devs)
		return &by_index[index]->name;
	else
		return NULL;
}



//static
String* rtlsdr_device::get_long_dev_descr(int index)
{
	if (index < num_devs)
		return &by_index[index]->long_description;
	else
		return NULL;
}



// let us assume that this only gets called alone, not related to rescan_devices
//static
int rtlsdr_device::set_current_device(int index)
{
	if (index < num_devs)
	{
		//who should invoke a rescan of gains for instance? The GUI probably...
		if (index != current_dev)
		{
			clear_gains_cur_dev();
			close_cur_dev();
			current_dev = index;
		}
		//else same dev, do nothing
		
		return index;
	}
	else
		return -1; // invalid index obtained (out of range)
}



//static
int rtlsdr_device::open_cur_dev()
{
    rtlsdr_device* this_dev = get_dev_ptr_by_idx(current_dev);
	if (this_dev != NULL)
		return this_dev->open();
	else
		return 0; // not an error, the device is already open... ?
}



//static
int rtlsdr_device::close_cur_dev()
{
	if (current_dev >= 0)
	{
	    rtlsdr_device* this_dev = get_dev_ptr_by_idx(current_dev);
		if (this_dev != NULL)
		{
			this_dev->close();
			return 0;
		}
		else
			return -1;
	}
	else // no device is open at the moment
		return 0;
}



int rtlsdr_device::rescan_gains()
{
	clear_gains();
	int tmp_num_gains = rtlsdr_get_tuner_gains(dev, NULL);
	if (tmp_num_gains > 0) // the retval indicates that there are some gain entries to learn
	{
		gains = (int*) malloc(sizeof(int) * tmp_num_gains);
		if (rtlsdr_get_tuner_gains(dev, gains) != tmp_num_gains) // error
		{
			clear_gains();
			tmp_num_gains = -1; // failed to actually get the list of tuner gains
		}
		else // the "gains" array now holds the number of gain entries specified.
			num_gains = tmp_num_gains;
	}
	//else do nothing
	
	return tmp_num_gains;
}

//static
int rtlsdr_device::rescan_gains_cur_dev()
{
    rtlsdr_device* this_dev = get_dev_ptr_by_idx(current_dev);
	if (this_dev != NULL)
		return this_dev->rescan_gains();
	else
		return -1;
}



int rtlsdr_device::get_gain(int gain_idx)
{
	if (dev != NULL)
		if (gains != NULL)
			if (num_gains > gain_idx)
				return gains[gain_idx];
		
	return(-1);
}

//static
int rtlsdr_device::get_gain_from_cur_dev(int gain_idx)
{
    rtlsdr_device* this_dev = get_dev_ptr_by_idx(current_dev);
	if (this_dev != NULL)
		return this_dev->get_gain(gain_idx);
	else
		return -1;
}


//static
int rtlsdr_device::get_num_gains_in_cur_dev()
{
    rtlsdr_device* this_dev = get_dev_ptr_by_idx(current_dev);
	if (this_dev != NULL)
		return this_dev->num_gains;
	else
		return -1;
}


int rtlsdr_device::set_gain(int gain_idx)
{
	String tmp_str;
	tmp_str << "Gonna set gain idx=" << gain_idx << " value=" << gains[gain_idx] << " dev: " << long_description;
	log_this(tmp_str);
	
	if (dev == NULL) // Not open. Internal error?
		return -1;
	if (gains == NULL) // Gains not initialized. Internal error?
		return -2;
	
	// Note: there are two different functions dealing with automatic gain:
	// rtlsdr_set_tuner_gain_mode() and rtlsdr_set_agc_mode().
	// Possibly the first one speaks about the external tuner front-end
	// and the second one speaks about a digital AGC inside the RTL2832.
	
	
	// set tuner gain mode to 1="manual" (the default = 0 = auto)
	if (rtlsdr_set_tuner_gain_mode(dev,1) != 0)
		return -3; // failed to set the manual gain mode
	

	// set the desired fixed gain value
	if (rtlsdr_set_tuner_gain(dev,gains[gain_idx]) != 0)
		return -4; // failed to set tuner gain
	
	
	// disable RTL2832 digital AGC
	if (rtlsdr_set_agc_mode(dev, 0) != 0)
		return -5; // failed to disable the AGC
	
	//otherwise we're OK
	return 0;
}

//static
int rtlsdr_device::set_gain_to_cur_dev(int gain_idx)
{
    rtlsdr_device* this_dev = get_dev_ptr_by_idx(current_dev);
	if (this_dev != NULL)
		return this_dev->set_gain(gain_idx);
	else
		return -1;
}



int rtlsdr_device::set_samp_rate(uint32_t rate)
{
	String tmp_str = "Sampling rate: ";
	tmp_str << rate;
	log_this(tmp_str);
	
	return rtlsdr_set_sample_rate(dev, rate);
}

//static
int rtlsdr_device::set_samp_rate_to_cur_dev(uint32_t rate)
{
    rtlsdr_device* this_dev = get_dev_ptr_by_idx(current_dev);
	if (this_dev != NULL)
		return this_dev->set_samp_rate(rate);
	else
		return -1;
}



int rtlsdr_device::set_freq(uint32_t freq)
{
	String tmp_str = "set_freq: ";
	tmp_str << freq;
	log_this(tmp_str);

	return rtlsdr_set_center_freq(dev, freq);
}

//static
int rtlsdr_device::set_freq_to_cur_dev(uint32_t freq)
{
    rtlsdr_device* this_dev = get_dev_ptr_by_idx(current_dev);
	if (this_dev != NULL)
		return this_dev->set_freq(freq);
	else
		return -1;
}



int rtlsdr_device::reset_buf()
{
	return rtlsdr_reset_buffer(dev);
}

//static
int rtlsdr_device::reset_buf_in_cur_dev()
{
    rtlsdr_device* this_dev = get_dev_ptr_by_idx(current_dev);
	if (this_dev != NULL)
		return this_dev->reset_buf();
	else
		return -1;
}



int rtlsdr_device::read_one_buf(void* sync_buf, int len)
{
 int n_read;
 
	int retval = rtlsdr_read_sync(dev, sync_buf, len, &n_read);
	return retval;
}

//static
int rtlsdr_device::read_one_buf_from_cur_dev(void* sync_buf, int len)
{
    rtlsdr_device* this_dev = get_dev_ptr_by_idx(current_dev);
	if (this_dev != NULL)
		return this_dev->read_one_buf(sync_buf, len);
	else
		return -1;
}



void rtlsdr_device::close()
{
	if (dev != NULL)
	{
		rtlsdr_close(dev);
		dev = NULL;
	}
}



//static
// Turn device index into a pointer... is this function even necessary?
rtlsdr_device* rtlsdr_device::get_dev_ptr_by_idx(int index)
{
	if (num_devs > index)
		return by_index[index];
	else
		return NULL;
}


//static
int rtlsdr_device::get_dev_idx_by_serial(String& serial)
{
	// ArrayMap::Get() throws an exception when it doesn't find any entry.
	// I don't like exceptions ;-)
    int this_dev_idx = by_serial.Find(serial); // returns an index in the ArrayMap, not RTLSDR
    if (this_dev_idx > 0)
        return by_serial[this_dev_idx];
    else
        return -1;
}


//static
void rtlsdr_device::empty_dev_list()
{
	if (num_devs > 0)
	{
		Vector<rtlsdr_device*>::iterator i;
		for (i = by_index.Begin(); i != by_index.End(); i++)
			delete *i;
		by_serial.Clear();
		by_index.Clear();
	}
}



void rtlsdr_device::clear_gains()
{
	if (gains != NULL)
	{
		free(gains);
		gains = NULL;
		num_gains = 0;
	}
}

//static
int rtlsdr_device::clear_gains_cur_dev()
{
	if (current_dev >= 0)
	{
	    rtlsdr_device* this_dev = get_dev_ptr_by_idx(current_dev);
		if (this_dev != NULL)
		{
			this_dev->clear_gains();
			return 0;
		}
		else
			return -1;
	}
	//else nothing to clear

   return 0;
}





/*

### class backend ###

*/


/*
FFT takes a certain number of complex I+Q time-domain samples,
and produces that same number of complex frequency-domain lines.
*/
void backend::set_buf_size(int _samples_per_win, int _num_windows)
{
	samples_per_win = _samples_per_win;
	num_windows = _num_windows;

	String tmp_str = "samples_per_win: ";
	tmp_str << samples_per_win;
	tmp_str << " num_windows: ";
	tmp_str << num_windows;
	log_this(tmp_str);

	// samples, not bytes
	num_samples = samples_per_win * num_windows;
	/* The RTL_SDR needs two bytes for every complex sample */
	buf_size_sync = num_samples * 2;
}


void backend::alloc()
{
	//cleanup();
	
	if (sync_buf == NULL) sync_buf = new unsigned char[buf_size_sync]; // multiple FFT windows
	if (input_samples == NULL) input_samples = new Complex[num_samples]; // multiple FFT windows
	if (spectral_lines == NULL) spectral_lines = new Complex[samples_per_win];
	if (swap_buf == NULL) swap_buf = new double[samples_per_win/2];
	if (window_fn == NULL) window_fn = new double[samples_per_win];
	if (abs_values == NULL) abs_values = new double[samples_per_win];
	
	if (avgs == NULL) avgs = new double[samples_per_win];
	
	calc_window_fn();
	
	//iq = new iqbal(samples_per_win, num_windows); // mag and phase initialized to 0
}



void backend::alloc_calib()
{
	if (calib_data == NULL) calib_data = new double[samples_per_win];
}



void backend::cleanup()
{
	//if (iq != NULL) { delete iq; iq = NULL; }
	if (sync_buf != NULL) { delete sync_buf; sync_buf = NULL; }
	if (input_samples != NULL) { delete input_samples; input_samples = NULL; }
	if (spectral_lines != NULL) { delete spectral_lines; spectral_lines = NULL; }
	if (swap_buf != NULL) { delete swap_buf; swap_buf = NULL; }
	if (window_fn != NULL) { delete window_fn; window_fn = NULL; }
	if (abs_values != NULL) { delete abs_values; abs_values = NULL; }
				
	if (avgs != NULL) { delete avgs; avgs = NULL; }
}


void backend::invalidate_calibration()
{
	if (calib_data != NULL) { delete calib_data; calib_data = NULL; }
}


/* https://en.wikipedia.org/wiki/Window_function */
void backend::calc_window_fn()
{
	window_power_density = 0;
	
	for (int i=0; i < samples_per_win; i++)
	{
		// calculate individual window samples
		window_fn[i] = 0.5 - 0.5 * cos(2 * M_PI * i / samples_per_win);
		// we could as well calculate the power density symbolically as a fairly simple integral,
		// but ahh well... This is generic for any windowing function.
		window_power_density += window_fn[i];
	}
	
	window_power_density /= samples_per_win;
}



void backend::crunch_data(bool calibration)
{
	//debugging vars:
	double max_val = 0;
	String message = "";
	
	//bzero(avgs, samples_per_win * sizeof(double));
	memset(avgs, 0, samples_per_win * sizeof(double));
	//for (int i=0; i<samples_per_win; i++)
	//	avgs[i] = 0;

	// Copy the raw data from rtlsdr buffer (signed char per I/Q, interleaved)
	// to a Complex working buffer.
	// Turn the signed "byte touples" into C++/kissFFT Complex type (2x double).
	for (int sample = 0; sample < num_samples; sample++)
	{
		input_samples[sample] = Complex( (double)sync_buf[2*sample] - 128,
										 (double)sync_buf[2*sample + 1] - 128 );
		max_val = max( max_val, abs(input_samples[sample]) );
	}
	
	message << "Max sample val:" <<  max_val;
	log_this(message);
	
	// I/Q balancer:
	//  Disabled for the moment. Turns out that it's not necessary.
	//iq->optimize(input_samples); // This is rather CPU-intensive. A couple dozen FFT's across the whole buffer.
	//iq->correct(input_samples, input_samples); // This is a single iteration across the buffer
	// Note that the I+Q correction is still performed in the time domain.

	// Construct KissFFT config
    kiss_fft_cfg cf = kiss_fft_alloc(samples_per_win,0,0,0);

	// Per-window FFT and other processing
	for (int nth_window = 0; nth_window < num_windows; nth_window++)
	{
		int sample_shifted = nth_window * samples_per_win;
		
		// apply windowing function (has to be done before the FFT, unfortunately)
		for (int sample=0; sample < samples_per_win; sample++, sample_shifted++)
			input_samples[sample_shifted] *= window_fn[sample];
		
	    // run the FFT
		kiss_fft(cf,
				 // as input, take just a particular window of the whole batch
				 (kiss_fft_cpx*)&input_samples[nth_window * samples_per_win],
				 // consequently, the immediate output corresponds to a single window
		         (kiss_fft_cpx*)spectral_lines  // pass output by reference
		        );
		// Note that kiss_fft() has seamlessly copied a particular time-domain window
		// into the "single window" intermediate working buffer (frequency domain already)
		
		// We're not interested in the phase of the frequency domain lines, just the amplitude.
		// It also makes some sense to average the absolute amplitudes across several windows,
		// to get rid of some random noise... (that follows in the next step)
		for (int spectr_line = 0; spectr_line < samples_per_win; spectr_line++)
			abs_values[spectr_line] = abs( spectral_lines[spectr_line] );
		
		/*
		  A quote from KissFFT documentation (and e.g. FFTW is the same):
		    Note: frequency-domain data is stored from dc up to 2pi.
		    so cx_out[0] is the dc bin of the FFT and
		    cx_out[nfft/2] is the Nyquist bin (if exists)
		  In other words, we need to swap the halves of the buffer
		  to get a spectrum from -nyquist to +nyquist.
		  And, swapping the calculated abs.values takes half the time and extra tmp storage.
		*/
	    memcpy(swap_buf, abs_values, (samples_per_win/2) * sizeof(double));
	    memcpy(abs_values, &abs_values[samples_per_win/2], (samples_per_win/2) * sizeof(double));
	    memcpy(&abs_values[samples_per_win/2], swap_buf, (samples_per_win/2) * sizeof(double));
	    
	    // accumulate the per-window absolutes
		for (int spectr_line=0; spectr_line < samples_per_win; spectr_line++)
			avgs[spectr_line] += abs_values[spectr_line];
	}
	
	kiss_fft_cleanup();
		
	// normalize = divide by num_windows
	for (int spectr_line=0; spectr_line < samples_per_win; spectr_line++)
		avgs[spectr_line] /= num_windows;
	
	// Get rid of the DC bar in the middle, in a crude and naive way:
	// Just discard the DC bar and use an average of the two neighboring frequency bars.
	// Actually this helps somewhat, but even with the R820T, the DC spike is actually
	// more like 3 frequency bars wide. So we need to discard 3 bars, rather than just 1.
	// With the E4000, the DC peak is broader and needs to be tackled in some other way...
	int central_bar = samples_per_win/2;
	double center_substitute = (avgs[central_bar-2] + avgs[central_bar+2])/2;
	double dv_dt = (avgs[central_bar+2] - avgs[central_bar-2]) / 4;
	avgs[central_bar-1] = center_substitute - dv_dt;
	avgs[central_bar] = center_substitute;
	avgs[central_bar+1] = center_substitute + dv_dt;
	
    /*
	  The FFT spectral output so far is essentially a simple accumulated sum
      of all the convoluted time-domain samples (input[n]*unity_sinewave[n]).
      I.e., it also includes a product of samples_per_win and input resolution
      and window power density.
      We can remove all those coefficients, to normalize the output data.
      The goal is to convert the values to "deciBell full scale".
      Now... the magnitude samples would seem to contain voltage, rather than power
      = one would instinctively multiply the logarithm by 20 = the result would be
      deciBell over full scale Voltage. Yet... strangely, the dB readings seem to match
      the output of RtlSDR_Scanner if I multiply this by 10 - i.e. as if the spectral samples
      actually mean "power"... The 10*log() also seems to correspond to the tuner gain scale.
      The resolution of the I/Q samples is 8bit signed, i.e. max. amplitude is 128.
      And, divide by the square root of "samples per window" - but starting at 128.
      The window power density figure has already been normalized (samples_per_win removed).
      Also note that (regardless of resolution) a full-scale unclipped sinewave
      should read -3 dBFS in the normalized spectrum.
      We can do the normalization straight in the buffer where we already have the data.
    */
	for (int spectr_line=0; spectr_line < samples_per_win; spectr_line++)
		avgs[spectr_line] = 10 * log( avgs[spectr_line]
										/ sqrt(samples_per_win*128)
										/ window_power_density
										/ 128 // maximum amplitude of a "signed char" (_s8)
									);
	// is this supposed to be 20*log() by any chance? See also the comment above.

	/* We now have the spectrum in dBFS. Obviously the question is, how dBFS relates to dBmW.
	   The dBFS knows nothing about radio front-end gain, and even if we know the nominal
	   gain configured, we don't really have the radio gain calibrated... */

	// The tuners contain an IF filter that results in the spectral response across the channel
	// to be anything except flat. The following section is my naive attempt at ironing out
	// the filter's characteristic curve.
	if (calibration)
	{
		// We need to produce the "filter normalization curve" for later use.
		// We want the curve to have a 0 dB average gain, so we need to calculate
		// an average and subtract it.
		int calib_avg_gain = 0;
		
		for (int spectr_line=0; spectr_line < samples_per_win; spectr_line++)
			calib_avg_gain += avgs[spectr_line];
		calib_avg_gain /= samples_per_win;
		
		// The question here is... do we want to normalize the output spectrum,
		// still in avgs[], for display? Or, do we only normalize the calibration storage?
		// Design decision: let's display the curve already normalized.
		for (int spectr_line=0; spectr_line < samples_per_win; spectr_line++)
		{
			avgs[spectr_line] -= calib_avg_gain;
			calib_data[spectr_line] = avgs[spectr_line];
		}
	}
	else
	{
		if (apply_calib)
			// We need to apply the "filter normalization curve", created previously,
			// to the currently acquired spectral data
			if (calib_data != NULL)
				for (int spectr_line=0; spectr_line < samples_per_win; spectr_line++)
					avgs[spectr_line] -= calib_data[spectr_line];
			//else no calibration data available = nothing to apply
		//else we should not apply the calibration curve
	}
}



int backend::read_one_buf()
{
	// buf_size_sync already amounts to an integer number of whole FFT windows
	return rtlsdr_device::read_one_buf_from_cur_dev(sync_buf, buf_size_sync);
}



int backend::scan_one_freq()
{
 int retval = 0;
 
	if (set_samp_rate())
	{
		log_this("Failed to set the sampling rate!");
		retval = -2;
		goto over;
	}
	
	if (set_gain())
	{
		log_this("Failed to set the gain!");
		retval = -1;
		goto over;
	}
	
	if (set_freq())
	{
		log_this("Failed to set the center frequency!");
		retval = -3;
		goto over;
	}
	
	// for the moment, let's abstract from PPM error
	// set direct IF sampling? Or do we definitely not want this? Nah, we don't.
	
	if (reset_buf())
	{
		log_this("Failed to reset the buffer!");
		retval = -4;
		goto over;
	}
	
	if (read_one_buf()) // actually num_windows * "FFT window"
	{
		log_this("Failed to read one buffer!");
		retval = -5;
		goto over;
	}
over:
	return retval;
}


// round down the binary power to a neat nearby decimal order
int samp_rate_to_bandwidth(int samp_rate)
{
 int bandwidth = 0;
 
	switch (samp_rate)
	{
		case 2048000: bandwidth = 2000000; break;
		case 1024000: bandwidth = 1000000; break;
		case  256000: bandwidth =  250000; break;
		default: break;
	}
 
	return bandwidth;
}
