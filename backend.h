#ifndef _rtlsdr_skyline_backend_h_
#define _rtlsdr_skyline_backend_h_

//using namespace Upp;

/*
This is the backend part of RTL-SDR Skyline,
encapsulating the RTL-SDR, the KissFFT
and the internal follow-up "skyline clockwork".

The backend assumes only ever using one RTL-SDR device at a time.
Should the Skyline get extended to support multiple RTL-SDR devices
simultaneously for some reason, the backend needs to get modified.
The notion of "current device" would become obsolete.
*/

#include <rtl-sdr.h>

/*
The RTL-SDR library can use two styles of reading:
	1) sync - you provide the buffer and perform a blocking "read" in a loop
	2) async - apparently the library allocates a specified number of buffer segments
	  of specified size (amounting to a segmented circular buffer),
	  and runs a thread that keeps calling your callback, handing buffer segments to it.

The buffer segment size must be a multiple of 512 Bytes,
and for some reason, the rtl-sdr.c example app defines it as a multiple of 16.
The default number of segments is 32 (that's what you get if you specify a 0 to the
async read request).
The buffer segment size for continuous async reading should probably be large enough
to keep the number of "context switches" reasonably low.
Like a couple dozen per second seems reasonable.

For a simple scenario where we do not want to delve into FFT trickeries all that much,
a single sync read of a relatively small buffer is perhaps the right way to go about things.
A single read per frequency. Keep iterating across frequencies and taking a sip.

Or we could use a more complicated route,
taking a longer continuous recording at each frequency,
probably using the segmented buffer and async reading
and a consumer thread to do the painting "in the background".
*/

#include <CtrlLib/CtrlLib.h>
using namespace Upp;
//#include <Map>
#include <Vector>
#include <String>
//#include "iqbal.h"

//#define BUF_SEG_SIZE_ASYNC 64*1024 // not used at the moment (async reading not used, maybe later)

class rtlsdr_device
{
public:
	rtlsdr_device(int _idx) : dev(NULL), idx(_idx), gains(NULL), num_gains(0) {}
	~rtlsdr_device() { clear_gains(); close(); }
	
	// Maybe do not perform a device rescan automatically on every run of the grabber...
	static int get_device_count_from_driver() { return rtlsdr_get_device_count(); }
	
	// After rescan_devices, your GUI may want to call get_current_device()
	// as rescan_devices() tries to look for a previously known device by S/N and stick to it.
	// A previously known device will nonetheless get closed, and its gains emptied.
	static int rescan_devices(); // retval: number of devices found upon rescan
	
	// the index arg is an index in the rtlsdr library
	//  (not necessarily the same as in the Vector or ArrayMap below, though possibly yes)
	static String* get_device_name(int index);
	static String* get_long_dev_descr(int index);
	static int get_current_device() { return current_dev; }
	static int set_current_device(int index); // returns index if OK, -1 if index invalid
	
	// Open the current device
	// returns the result of rtlsdr_open, i.e. >= 0 if OK, < 0 if something went wrong
	int open() { return rtlsdr_open(&dev, idx); }
	static int open_cur_dev();
	void close(); // will close the underlying rtlsdr_device if still open
	static int close_cur_dev();
	int rescan_gains(); // returns the number (count) of gain entries learned
	static int rescan_gains_cur_dev();
	int get_gain(int gain_idx); // returns the payload of the gain entry indicated, in db tenths
	static int get_gain_from_cur_dev(int gain_idx);
	static int get_num_gains_in_cur_dev();
	int set_gain(int gain_idx);
	static int set_gain_to_cur_dev(int gain_idx);
	int set_samp_rate(uint32_t rate);
	static int set_samp_rate_to_cur_dev(uint32_t rate);
	int set_freq(uint32_t freq);
	static int set_freq_to_cur_dev(uint32_t freq);
	int reset_buf();
	static int reset_buf_in_cur_dev();
	int read_one_buf(void* sync_buf, int len);
	static int read_one_buf_from_cur_dev(void* sync_buf, int len);
	
	String name;
	String manufact;
	String product;
	String serial;
	static int num_devs;
	
private:
	static rtlsdr_device* get_dev_ptr_by_idx(int index);
	static int get_dev_idx_by_serial(String& serial); // retval < 0 == error
	static void empty_dev_list();
	void clear_gains();
	static int clear_gains_cur_dev();
	
	static ArrayMap<String,int> by_serial; // the Map index is possibly arbitrary?
	static Vector<rtlsdr_device*> by_index; // here the ordering by index should be persistent
	static int current_dev; // to remember across rescans and signal that there was a past
	static String current_dev_serial; // to remember across rescans
	
	String long_description; // we want to pass around a pointer/ref, not a value...
	rtlsdr_dev_t* dev;
	int idx;
	int* gains;
	int num_gains;
};


// The backend encapsulates the rtl_sdr library, a set of rtlsdr devices,
// and some FFT-based math.
// In the current version of this code, only one rtl_sdr device can be active at any one time.
class backend
{
public:
	backend() :
		gain_idx(-1),
		freq(0),
		samp_rate(0),
		samples_per_win(0),
		num_windows(0),
		num_samples(0),
		buf_size_sync(0),
		apply_calib(true),
		avgs(NULL),
		calib_data(NULL),
		sync_buf(NULL),
		input_samples(NULL),
		spectral_lines(NULL),
		swap_buf(NULL),
		window_fn(NULL),
		abs_values(NULL)
		//iq(NULL)
	 {}
	 
	// close device and deallocate the various crunching buffers
	~backend() { close(); cleanup(); invalidate_calibration(); }
	
	int get_device_count_from_driver() { return rtlsdr_device::get_device_count_from_driver(); }
	int get_device_count() { return rtlsdr_device::num_devs; }
	int rescan_devices() { close(); return rtlsdr_device::rescan_devices(); }
	String* get_device_name(int index) { return rtlsdr_device::get_device_name(index); }
	String* get_long_dev_descr(int index) { return rtlsdr_device::get_long_dev_descr(index); };
	int get_current_device() {return rtlsdr_device::get_current_device(); }
	int set_current_device(int index) { return rtlsdr_device::set_current_device(index); }
	int open(){ return rtlsdr_device::open_cur_dev(); }
	int close(){ return rtlsdr_device::close_cur_dev(); }
	int rescan_gains() { return rtlsdr_device::rescan_gains_cur_dev(); }
	int get_gain(int gain_idx) { return rtlsdr_device::get_gain_from_cur_dev(gain_idx); }
	int get_num_gains_in_cur_dev() { return rtlsdr_device::get_num_gains_in_cur_dev(); }
	int set_gain() { return rtlsdr_device::set_gain_to_cur_dev(gain_idx); }
	int set_samp_rate() { return rtlsdr_device::set_samp_rate_to_cur_dev(samp_rate); }
	void set_buf_size(int _samples_per_win, int _num_windows);
	int set_freq() { return rtlsdr_device::set_freq_to_cur_dev(freq); }
	void alloc();
	void alloc_calib();
	void cleanup();
	void invalidate_calibration();
	int scan_one_freq();	/* Scan_one_freq calls alloc() internally. */
	void crunch_data(bool calibration=false);

	int gain_idx;  // rtlsdr index
	uint32_t freq; // center frequency to tune to
	uint32_t samp_rate;
	int samples_per_win;
	int num_windows;
	int num_samples; // Complex samples, per batch (per a frequency scanned)
	int buf_size_sync; // bytes obtained from rtl_sdr, two bytes per sample
	bool apply_calib; // we can opt not to apply the filter calibration curve

	double* avgs; // the math output for display, in dBFS
	double* calib_data; // the curve for "IF filter response inversion"

private:
	void calc_window_fn();
	int reset_buf() { return rtlsdr_device::reset_buf_in_cur_dev(); }
	int read_one_buf();
		
	unsigned char* sync_buf;
	Complex* input_samples;
	Complex* spectral_lines;
	double* swap_buf; // SAMPLES_PER_WINDOW is a power of 2 = always even
	double* window_fn;
	double window_power_density; // integral across the window fn
	double* abs_values; // last step in the per-window workflow
	//iqbal* iq; // doesn't seem to be needed after all, there are no mirror phantom images
};

// round the binary samp.rate to a nearby decimal bandwidth
int samp_rate_to_bandwidth(int samp_rate);


#endif
