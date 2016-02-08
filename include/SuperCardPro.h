#ifndef SUPERCARDPRO_H
#define SUPERCARDPRO_H

class SuperCardPro
{
public:
	static const int MAX_FLUX_REVS = 5;			// firmware has this hard limit
	static const int NS_PER_BITCELL = 25;		// 25ns per tick in flux times

	static const uint8_t pr_Unused = 0x00;		// not used (to disallow NULL responses)
	static const uint8_t pr_BadCommand = 0x01;	// bad command
	static const uint8_t pr_CommandErr = 0x02;	// command error (bad structure, etc.)
	static const uint8_t pr_Checksum = 0x03;	// packet checksum failed
	static const uint8_t pr_Timeout = 0x04;		// USB timeout
	static const uint8_t pr_NoTrk0 = 0x05;		// track zero never found (possibly no drive online)
	static const uint8_t pr_NoDriveSel = 0x06;	// no drive selected
	static const uint8_t pr_NoMotorSel = 0x07;	// motor not enabled (required for read or write)
	static const uint8_t pr_NotReady = 0x08;	// drive not ready (disk change is high)
	static const uint8_t pr_NoIndex = 0x09;		// no index pulse detected
	static const uint8_t pr_ZeroRevs = 0x0A;	// zero revolutions chosen
	static const uint8_t pr_ReadTooLong = 0x0B;	// read data was more than RAM would hold
	static const uint8_t pr_BadLength = 0x0C;	// length value invalid
	static const uint8_t pr_BadData = 0x0D;		// bit cell time is invalid (0)
	static const uint8_t pr_BoundaryOdd = 0x0E;	// location boundary is odd
	static const uint8_t pr_WPEnabled = 0x0F;	// disk is write protected
	static const uint8_t pr_BadRAM = 0x10;		// RAM test failed
	static const uint8_t pr_NoDisk = 0x11;		// no disk in drive
	static const uint8_t pr_BadBaud = 0x12;		// bad baud rate selected
	static const uint8_t pr_BadCmdOnPort = 0x13;// command is not available for this type of port
	static const uint8_t pr_Ok = 0x4F;			// packet good (letter 'O' for OK)

public:
	static std::unique_ptr<SuperCardPro> Open ();
	virtual ~SuperCardPro () = default;

public:
	bool SelectDrive (int drive);
	bool DeselectDrive (int drive);
	bool EnableMotor (int drive);
	bool DisableMotor (int drive);
	bool Seek0 ();
	bool StepTo (int cyl);
	bool StepIn ();
	bool StepOut ();
	bool SelectDensity (bool high);
	bool SelectSide (int head);
	bool GetDriveStatus (int &status);
	bool GetParameters (int &drive_select_delay, int &step_delay, int &motor_on_delay, int &seek_0_delay, int &motor_off_delay);
	bool SetParameters (int drive_select_delay, int step_delay, int motor_on_delay, int seek_0_delay, int motor_off_delay);
	bool RamTest ();
	bool SetPin33 (bool high);
	bool ReadFlux (int revs, std::vector<std::vector<uint32_t>> &flux_revs);
	bool WriteFlux (const void *p, int nr_bitcells);
	bool GetInfo (int &hwversion, int &fwversion);

	uint8_t GetErrorStatus () const { return m_error; }
	std::string GetErrorStatusText () const;

private:
	static const uint8_t CHECKSUM_INIT = 0x4a;

	static const uint8_t CMD_SELA = 0x80;			// select drive A
	static const uint8_t CMD_SELB = 0x81;			// select drive B
	static const uint8_t CMD_DSELA = 0x82;			// deselect drive A
	static const uint8_t CMD_DSELB = 0x83;			// deselect drive B
	static const uint8_t CMD_MTRAON = 0x84;			// turn motor A on
	static const uint8_t CMD_MTRBON = 0x85;			// turn motor B on
	static const uint8_t CMD_MTRAOFF = 0x86;		// turn motor A off
	static const uint8_t CMD_MTRBOFF = 0x87;		// turn motor B off
	static const uint8_t CMD_SEEK0 = 0x88;			// seek track 0
	static const uint8_t CMD_STEPTO = 0x89;			// step to specified track
	static const uint8_t CMD_STEPIN = 0x8A;			// step towards inner (higher) track
	static const uint8_t CMD_STEPOUT = 0x8B;		// step towards outer (lower) track
	static const uint8_t CMD_SELDENS = 0x8C;		// select density
	static const uint8_t CMD_SIDE = 0x8D;			// select side
	static const uint8_t CMD_STATUS = 0x8E;			// get drive status
	static const uint8_t CMD_GETPARAMS = 0x90;		// get parameters
	static const uint8_t CMD_SETPARAMS = 0x91;		// set parameters
	static const uint8_t CMD_RAMTEST = 0x92;		// do RAM test
	static const uint8_t CMD_SETPIN33 = 0x93;		// set pin 33 of floppy connector
	static const uint8_t CMD_READFLUX = 0xA0;		// read flux level
	static const uint8_t CMD_GETFLUXINFO = 0xA1;	// get info for last flux read
	static const uint8_t CMD_WRITEFLUX = 0xA2;		// write flux level
	static const uint8_t CMD_READMFM = 0xA3;		// read MFM level
	static const uint8_t CMD_GETMFMINFO = 0xA4;		// get info for last MFM read
	static const uint8_t CMD_WRITEMFM = 0xA5;		// write MFM level
	static const uint8_t CMD_READGCR = 0xA6;		// read GCR level
	static const uint8_t CMD_GETGCRINFO = 0xA7;		// get info for last GCR read
	static const uint8_t CMD_WRITEGCR = 0xA8;		// write GCR level
	static const uint8_t CMD_SENDRAM_USB = 0xA9;	// send data from buffer to USB
	static const uint8_t CMD_LOADRAM_USB = 0xAA;	// get data from USB and store in buffer
	static const uint8_t CMD_SENDRAM_232 = 0xAB;	// send data from buffer to the serial port
	static const uint8_t CMD_LOADRAM_232 = 0xAC;	// get data from the serial port and store in buffer
	static const uint8_t CMD_FLUX2GCR = 0xB0;		// convert flux data to GCR data
	static const uint8_t CMD_GCR2SECCBM = 0xB1;		// decode CBM GCR track as sectors
	static const uint8_t CMD_READCBMDISK = 0xB2;	// read CBM disk as D64
	static const uint8_t CMD_WRITECBMDISK = 0xB3;	// write CBM disk from D64
	static const uint8_t CMD_OPENFILE = 0xC0;		// open FAT16/32 file
	static const uint8_t CMD_CLOSEFILE = 0xC1;		// close FAT16/32 file
	static const uint8_t CMD_READFILE = 0xC2;		// read from open file
	static const uint8_t CMD_WRITEFILE = 0xC3;		// write to open file
	static const uint8_t CMD_SEEKFILE = 0xC4;		// seek to position in file
	static const uint8_t CMD_DELETEFILE = 0xC5;		// delete file
	static const uint8_t CMD_FILEINFO = 0xC6;		// get file info
	static const uint8_t CMD_DIR = 0xC7;			// show directory
	static const uint8_t CMD_CHDIR = 0xC8;			// change directory
	static const uint8_t CMD_MKDIR = 0xC9;			// make new directory
	static const uint8_t CMD_RMDIR = 0xCA;			// remove directory
	static const uint8_t CMD_SCPINFO = 0xD0;		// get info about SCP hardware/firmware
	static const uint8_t CMD_SETBAUD1 = 0xD1;		// sets the baud rate of port labeled RS232-1
	static const uint8_t CMD_SETBAUD2 = 0xD2;		// sets the baud rate of port labeled RS232-2

	// Flags for CMD_READFLUX
	static const uint8_t ff_Index = 0x01;           // 0 = immediate read, 1 = wait on index pulse before read
	static const uint8_t ff_BitCellSize = 0x02;     // 0 = 16-bit cell size, 1 = 8-bit cell size
	static const uint8_t ff_Wipe = 0x04;            // 0 = no wipe before write, 1 = wipe track before write
	static const uint8_t ff_RPM360 = 0x08;          // 0 = 300 RPM drive, 1 = 360 RPM drive

//	bool SetError (uint8_t error);
	bool SendCmd (uint8_t cmd, void *p = NULL, int len = 0, void *readbuf = NULL, int readlen = 0);
	bool ReadExact (void *buf, int len);
	bool WriteExact (const void *buf, int len);

protected:
	SuperCardPro () : m_error(0) {}
	virtual bool Read (void *p, int len, int *bytes_read) = 0;
	virtual bool Write (const void *p, int len, int *bytes_written) = 0;

	uint8_t m_error;
};

#endif // SUPERCARDPRO_H
