#ifndef IBM_PC_H
#define IBM_PC_H

const int GAP2_MFM_ED = 41;	// gap2 for MFM 1Mbps (ED)
const int GAP2_MFM_DDHD = 22;	// gap2 for MFM, except 1Mbps (ED)
const int GAP2_FM = 11;	// gap2 for FM (same bit size as MFM due to encoding)

const int TRACK_OVERHEAD_MFM = 80/*x0x4e=gap4a*/ + 12/*x0x00=sync*/ + 4/*3x0c2+0xfc=iam*/ + 50/*x0x4e=gap1*/;	// = 146
const int SECTOR_OVERHEAD_MFM = 12/*x0x00=sync*/ + 4/*3xa1+0xfe=idam*/ + 4/*CHRN*/ + 2/*crc*/ + 22/*x0x4e=gap2*/ +
12/*x0x00=sync*/ + 4/*3x0xa1+0xfb*/ /*+ data_size*/ + 2/*crc*/ /*+ gap3*/;		// = 62
const int DATA_OVERHEAD_MFM = 4/*3x0xa1+0xfb*/;
const int SYNC_OVERHEAD_MFM = 12/*x0x00=sync*/;
const int SECTOR_OVERHEAD_ED = GAP2_MFM_ED - GAP2_MFM_DDHD;

const int TRACK_OVERHEAD_FM = 40/*x0x00=gap4a*/ + 6/*x0x00=sync*/ + 1/*0xfc=iam*/ + 26/*x0x00=gap1*/;		// = 73

const int SECTOR_OVERHEAD_FM = 6/*x0x00=sync*/ + 1/*0xfe=idam*/ + 4/*CHRN*/ + 2/*crc*/ + 11/*x0x00=gap2*/ +
6/*x0x00=sync*/ + 1/*0xfb*/ /*+ data_size*/ + 2/*crc*/ /*+ gap3*/;				// = 33
const int DATA_OVERHEAD_FM = 1/*0xfb*/;
const int SYNC_OVERHEAD_FM = 6/*x0x00=sync*/;

const int MIN_GAP3 = 1;
const int MAX_GAP3 = 82;	// arbitrary size, to leave a bit more space at the track end

//#define SAFE_TRACKS_80		83		// Safe seek for 80-track drives
//#define SAFE_TRACKS_40		42		// Safe seek for 40-track drives

const int RPM_TIME_200 = 300000;
const int RPM_TIME_300 = 200000;
const int RPM_TIME_360 = 166667;

const auto SIZE_MASK_765 = 7U;

// uPD765 status register 0
const uint8_t STREG0_INTERRUPT_CODE = 0xc0;
const uint8_t STREG0_SEEK_END = 0x20;
const uint8_t STREG0_EQUIPMENT_CHECK = 0x10;
const uint8_t STREG0_NOT_READY = 0x08;
const uint8_t STREG0_HEAD_ADDRESS = 0x04;
const uint8_t STREG0_UNIT_SELECT_1 = 0x02;
const uint8_t STREG0_UNIT_SELECT_0 = 0x01;

// uPD765 status register 1
const uint8_t STREG1_END_OF_CYLINDER = 0x80;
const uint8_t STREG1_RESERVED_6 = 0x40;
const uint8_t STREG1_DATA_ERROR = 0x20;
const uint8_t STREG1_OVERRUN = 0x10;
const uint8_t STREG1_RESERVED_3 = 0x08;
const uint8_t STREG1_NO_DATA = 0x04;
const uint8_t STREG1_NOT_WRITEABLE = 0x02;
const uint8_t STREG1_MISSING_ADDRESS_MARK = 0x01;

// uPD765 status register 2
const uint8_t STREG2_RESERVED_7 = 0x80;
const uint8_t STREG2_CONTROL_MARK = 0x40;
const uint8_t STREG2_DATA_ERROR_IN_DATA_FIELD = 0x20;
const uint8_t STREG2_WRONG_CYLINDER = 0x10;
const uint8_t STREG2_SCAN_EQUAL_HIT = 0x08;
const uint8_t STREG2_SCAN_NOT_SATISFIED = 0x04;
const uint8_t STREG2_BAD_CYLINDER = 0x02;
const uint8_t STREG2_MISSING_ADDRESS_MARK_IN_DATA_FIELD = 0x01;

#ifdef _WIN32
std::string to_string(const MEDIA_TYPE &type);
#endif

int GetDataTime (DataRate datarate, Encoding encoding, int len_bytes = 1, bool add_drain_time = false);
int GetTrackOverhead (Encoding encoding);
int GetSectorOverhead (Encoding encoding);
int GetDataOverhead (Encoding encoding);
int GetSyncOverhead (Encoding encoding);
int GetRawTrackCapacity (int drive_speed, DataRate datarate, Encoding encoding);
int GetTrackCapacity (int drive_speed, DataRate datarate, Encoding encoding);
int GetFormatLength (Encoding encoding, int sectors, int size, int gap3);
int GetUnformatSizeCode (DataRate datarate);
int GetFormatGap (int drive_speed, DataRate datarate, Encoding encoding, int sectors, int size);

#endif // IBM_PC_H
