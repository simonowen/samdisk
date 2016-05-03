#ifndef KRYOFLUX_H
#define KRYOFLUX_H

class KryoFlux
{
public:
	static const uint16_t KF_VID = 0x03eb;
	static const uint16_t KF_PID = 0x6124;
	static const uint8_t KF_EP_BULK_OUT = 0x01;
	static const uint8_t KF_EP_BULK_IN = 0x82;
	static const int KF_INTERFACE = 1;
	static const int KF_TIMEOUT_MS = 1500;
	static const int KF_FW_LOAD_ADDR = 0x202000;
	static const int KF_FW_EXEC_ADDR = KF_FW_LOAD_ADDR;
	static const char * KF_FW_FILE;
	static const uint8_t OOB = 0x0d;

public:
	static std::unique_ptr<KryoFlux> Open ();
	virtual ~KryoFlux () = default;

public:
	int Reset ();
	int SelectDevice (int device);
	int EnableMotor (int drive);
	int Seek (int cyl);
	int SelectDensity (bool high);
	int SelectSide (int head);
	int SetMinTrack (int cyl);
	int SetMaxTrack (int cyl);
	int GetInfo (int index, std::string &info);

	void ReadFlux (int indexes, FluxData &flux_revs, std::vector<std::string> &warnings);
	static FluxData DecodeStream (const Data &data, std::vector<std::string> &warnings);

private:
	static const int REQ_STATUS = 0x00;					// status
	static const int REQ_INFO = 0x01;					// info (index 1 or 2)
	static const int REQ_RESULT = 0x02;
	static const int REQ_DATA = 0x03;
	static const int REQ_INDEX = 0x04;					// index positions from last read?
	static const int REQ_RESET = 0x05;					// soft reset
	static const int REQ_DEVICE = 0x06;					// select device
	static const int REQ_MOTOR = 0x07;					// motor state
	static const int REQ_DENSITY = 0x08;				// select density
	static const int REQ_SIDE = 0x09;					// select side
	static const int REQ_TRACK = 0x0a;					// seek
	static const int REQ_STREAM = 0x0b;					// stream on/off, MSB=revs
	static const int REQ_MIN_TRACK = 0x0c;				// set min track (default=0)
	static const int REQ_MAX_TRACK = 0x0d;				// set max track (default=81)
	static const int REQ_T_SET_LINE = 0x0e;				// (default=4)
	static const int REQ_T_DENSITY_SELECT =0x0f;		// (default=500000)
	static const int REQ_T_DRIVE_SELECT = 0x10;			// (default=60000)
	static const int REQ_T_SIDE_SELECT = 0x11;			// (default=1000)
	static const int REQ_T_DIRECTION_SELECT = 0x12;		// (default=12)
	static const int REQ_T_SPIN_UP = 0x13;				// (default=800000)
	static const int REQ_T_STEP_AFTER_MOTOR = 0x14;		// (default=200000)
	static const int REQ_T_STEP_SIGNAL = 0x15;			// (default=4)
	static const int REQ_T_STEP = 0x16;					// (default=8000)
	static const int REQ_T_TRACK0_SIGNAL = 0x17;		// (default=1000)
	static const int REQ_T_DIRECTION_CHANGE = 0x18;		// (default=38000)
	static const int REQ_T_HEAD_SETTLING = 0x19;		// (default=40000)
	static const int REQ_T_WRITE_GATE_OFF = 0x20;		// (default=1200)
	static const int REQ_T_WRITE_GATE_ON = 0x21;		// (default=8)
	static const int REQ_T_BYPASS_OFF = 0x22;			// (default=8)
	static const int REQ_T_BYPASS_ON = 0x23;			// (default=8)

	constexpr static int REQ_GET = 0x80;				// read modifier for requests above


	void SamBaCommand (const std::string &cmd, const std::string &end="");
	void UploadFirmware ();
	static int ResponseCode (const std::string &str);

protected:
	KryoFlux () = default;

	virtual std::string GetProductName () = 0;

	virtual std::string Control (int req, int index=0, int value=0) = 0;
	virtual int Read (void *buf, int len) = 0;
	virtual int Write (const void *buf, int len) = 0;
	virtual void StartRead (void *, int) { }
	virtual int WaitRead () { return 0; }
};

#endif // KRYOFLUX_H
