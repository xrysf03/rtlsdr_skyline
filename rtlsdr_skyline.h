#ifndef _rtlsdr_skyline_rtlsdr_skyline_h
#define _rtlsdr_skyline_rtlsdr_skyline_h

#include <CtrlLib/CtrlLib.h>
#include <ScatterCtrl/ScatterCtrl.h>

using namespace Upp;

// declaring your own WhenGotFocus/WhenLostFocus events / callbacks
struct MyEditDouble : EditDouble {	// Any derived-class to be used in the layout editor
	Event<> WhenGotFocus;			// must be declared before the layout file.
	Event<> WhenLostFocus;
	void GotFocus()  override { WhenGotFocus();  };
	void LostFocus() override { WhenLostFocus(); };
};

#define LAYOUTFILE <rtlsdr_skyline/rtlsdr_skyline.lay>
#include <CtrlCore/lay.h>

#define IMAGEFILE  <rtlsdr_skyline/thumb.iml>
#define IMAGECLASS ThumbImg
#include <Draw/iml.h>
#include "backend.h"

class rtlsdr_skyline : public Withrtlsdr_skylineLayout<TopWindow> {
public:
	typedef rtlsdr_skyline CLASSNAME;
	rtlsdr_skyline();

	void rescan_devices();
	void engage_device();
	void rescan_gains();
	void clear_gains();
	int set_gain();
	int set_samp_rate(int samp_rate);
	int reset_buf();
	int read_one_buf();
	
	void scan_band_crude();
	void calibrate();
	void invalidate_calibration();

	//void button_close_click();
	void button_rescan_click();
	void button_go_click();
	void button_calib_click();
	void gain_selected();
	void edit_from_action();
	void edit_to_action();
	void droplist_win_size_action();
	void droplist_rate_action();
	void member_log_this(const char* log_msg);
	
private:
	String log_text;
	backend backend;
	double* buffer; // For the skyline chart. Allocated dynamically.
	double from_freq_rounded, to_freq_rounded;
	bool display_in_calib_mode;
	
	void scroll_log(); // all the way down
	
	void on_close(); // give SkyLine a chance to bid farewell (graceful shutdown / cleanup)
	double chart1_optimal_major_unit_x();
	void blank_out_chart_grid();
	void set_chart_grid();
	void enter_scan_lock();
	void leave_scan_lock();
	void stop_scanning();
	
	//Event<> WhenLayout; // not even necessary
	void Layout() override
	{
		// first and foremost, do whatever you need to with all the widgets (resize etc)
		set_chart_grid(); // and then finally, recalculate and apply the horizontal tick spacing
		Withrtlsdr_skylineLayout<TopWindow>::Layout();
		Chart1.Refresh();
		Ctrl::ProcessEvents();
	}
};

#endif
