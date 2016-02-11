// HFE format for HxC floppy emulator:
//  http://hxc2001.com/download/floppy_drive_emulator/SDCard_HxC_Floppy_Emulator_HFE_file_format.pdf

#include "SAMdisk.h"
#include "DemandDisk.h"

// Note: currently only format revision 00 is supported.

#define HFE_SIGNATURE	"HXCPICFE"

typedef struct
{
	uint8_t header_signature[8];
	uint8_t format_revision;
	uint8_t number_of_track;
	uint8_t number_of_side;
	uint8_t track_encoding;
	uint8_t bitRate_LSB;				// in Kbps
	uint8_t bitRate_MSB;
	uint8_t floppyRPM_LSB;
	uint8_t floppyRPM_MSB;
	uint8_t floppy_interface_mode;
	uint8_t do_not_use;
	uint8_t track_list_offset_LSB;		// in 512-byte blocks
	uint8_t track_list_offset_MSB;
	uint8_t write_allowed;
	uint8_t single_step;				// 0xff = normal, 0x00 = double-step
	uint8_t track0s0_altencoding;		// 0xff = ignore, otherwise use encoding below
	uint8_t track0s0_encoding;			// override encoding for track 0 head 0
	uint8_t track0s1_altencoding;		// 0xff = ignore, otherwise use encoding below
	uint8_t track0s1_encoding;			// override encoding for track 0 head 1
} HFE_HEADER;

typedef struct
{
	uint8_t offset_LSB;
	uint8_t offset_MSB;
	uint8_t track_len_LSB;
	uint8_t track_len_MSB;
} HFE_TRACK;

#if 0
enum HfeFloppyInterfaceMode
{
	IBMPC_DD_FLOPPYMODE = 0,
	IBMPC_HD_FLOPPYMODE,
	ATARIST_DD_FLOPPYMODE,
	ATARIST_HD_FLOPPYMODE,
	AMIGA_DD_FLOPPYMODE,
	AMIGA_HD_FLOPPYMODE,
	CPC_DD_FLOPPYMODE,
	GENERIC_SHUGGART_DD_FLOPPYMODE,
	IBMPC_ED_FLOPPYMODE,
	MSX2_DD_FLOPPYMODE,
	C64_DD_FLOPPYMODE,
	EMU_SHUGART_FLOPPYMODE,
	S950_DD_FLOPPYMODE,
	S950_HD_FLOPPYMODE,
	DISABLE_FLOPPYMODE = 0xfe
};

enum HfeTrackEncoding
{
	ISOIBM_MFM_ENCODING = 0,
	AMIGA_MFM_ENCODING,
	ISOIBM_FM_ENCODING,
	EMU_FM_ENCODING,
	UNKNOWN_ENCODING = 0xff
};
#endif


bool ReadHFE (MemFile &file, std::shared_ptr<Disk> &disk)
{
	HFE_HEADER hh;
	if (!file.rewind() || !file.read(&hh, sizeof(hh)) || memcmp(&hh.header_signature, HFE_SIGNATURE, sizeof(hh.header_signature)))
		return false;

	if (hh.format_revision != 0)
		throw util::exception("unsupported HFE format revision (", hh.format_revision, ")");

	HFE_TRACK aTrackLUT[256];
	auto track_lut_offset = ((hh.track_list_offset_MSB << 8) | hh.track_list_offset_LSB) << 9;
	if (!file.seek(track_lut_offset) || !file.read(aTrackLUT, sizeof(aTrackLUT)))
		throw util::exception("failed to read track LUT (@", track_lut_offset, ")");

	DataRate datarate;
	auto data_bitrate = (hh.bitRate_MSB << 8) | hh.bitRate_LSB;
	if (data_bitrate >= 240 && data_bitrate <= 260)
		datarate = DataRate::_250K;
	else if (data_bitrate >= 290 && data_bitrate <= 310)
		datarate = DataRate::_300K;
	else if (data_bitrate >= 490 && data_bitrate <= 510)
		datarate = DataRate::_500K;
	else if (data_bitrate == -1)
		throw util::exception("variable bitrate images are not supported");
	else
		throw util::exception("unsupported data rate (", data_bitrate, "Kbps)");

	ValidateGeometry(hh.number_of_track, hh.number_of_side);

	// 64K should be enough for maximum MFM track size, and we'll check later anyway
	MEMORY mem(0x10000);
	auto pbTrack = mem.pb;

	auto hfe_disk = std::make_shared<DemandDisk>();

	for (uint8_t cyl = 0; cyl < hh.number_of_track && !g_fAbort; cyl++)
	{
		// Offset is in 512-byte blocks, data length covers both heads
		auto uTrackDataOffset = ((aTrackLUT[cyl].offset_MSB << 8) | aTrackLUT[cyl].offset_LSB) << 9;
		auto uTrackDataLen = ((aTrackLUT[cyl].track_len_MSB << 8) | aTrackLUT[cyl].track_len_LSB) >> 1;

		if (uTrackDataLen > mem.size)
			throw util::exception("invalid track size (", uTrackDataLen, ") for track ", CylStr(cyl));

		for (uint8_t head = 0; head < hh.number_of_side && !g_fAbort; head++)
		{
			// Head 1 data starts 256 bytes in
			if (head == 1)
				uTrackDataOffset += 256;

			auto uRead = 0;
			while (uRead < uTrackDataLen)
			{
				auto chunk = std::min(uTrackDataLen - uRead, 256);

				// Read the next interleaved chunk
				if (!file.seek(uTrackDataOffset + (uRead * 2)) || !file.read(pbTrack + uRead, chunk))
					throw util::exception("EOF reading track data for ", CH(cyl, head));

				uRead += chunk;
			}

			BitBuffer bitbuf(datarate, pbTrack, uTrackDataLen);
			hfe_disk->set_source(CylHead(cyl, head), std::move(bitbuf));

			// ToDo: save more track metadata?
		}
	}

	hfe_disk->strType = "HFE";
	disk = hfe_disk;

	return true;
}
