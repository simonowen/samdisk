// Fuzzy parser for raw track data

#include "SAMdisk.h"
#include "TrackDataParser.h"

// This class attempts to make sense of decoded bitstream data, as returned
// by FDCs from gap areas of the track. It allows for mis-synced data being
// returned as clock bits, splice noise, and mis-framed bytes.

TrackDataParser::TrackDataParser(const uint8_t *buf, int len)
	: _bitpos(0), _wrapped(false)
{
	SetData(buf, len);
}

void TrackDataParser::SetData (const uint8_t *buf, int len)
{
	_track_data = buf;
	_track_len = len;
}

void TrackDataParser::SetBitPos (int bitpos)
{
	// If before buffer start, wrap to end
	if (bitpos < 0)
	{
		if (_track_len)
			bitpos %= _track_len * 8;

		bitpos += _track_len * 8;
		_wrapped = false;
	}

	// If after buffer end, wrap to start
	if (bitpos / 8 >= _track_len)
	{
		if (_track_len)
			bitpos %= _track_len * 8;

		_wrapped = true;
	}

	// Set new position within buffer
	_bitpos = bitpos;
}

uint8_t TrackDataParser::ReadByte ()
{
	uint8_t b;

	// Convert bit position to byte offset+shift
	auto offset = _bitpos >> 3, shift = _bitpos & 7;

	// Advance by 8 bits
	_bitpos += 8;

	// Before final b
	if (offset != _track_len - 1)
		b = (_track_data[offset] << shift) | (_track_data[offset + 1] >> (8 - shift));
	else
	{
		b = (_track_data[offset] << shift) | (_track_data[0] >> (8 - shift));

		_bitpos -= _track_len * 8;
		_wrapped = true;
	}

	return b;
}

uint8_t TrackDataParser::ReadPrevByte ()
{
	SetBitPos(_bitpos - 8);
	auto b = ReadByte();
	SetBitPos(_bitpos - 16);
	return b;
}

int TrackDataParser::GetGapRun (int &out_len, uint8_t &out_fill, bool *unshifted)
{
	auto len = 0, min_run = 0, max_run = 0;
	uint8_t run_byte = 0xff;

	auto next_pos = GetBitPos() + 1;

	// End of buffer?
	if (IsWrapped())
	{
		out_len = -1;
		return false;
	}

	// Loop until the end of the data
	for (len = 0; !IsWrapped(); ++len)
	{
		auto b = ReadByte();

		// First byte?
		if (!len)
		{
			run_byte = b;

			// Check it's something valid for a run
			switch (run_byte)
			{
				// 4E or clock
				case 0x4e: case 0x21:
					min_run = 4;
					out_fill = 0x4e;
					if (unshifted) *unshifted &= (run_byte == out_fill);
					continue;

				// 00 or clock
				case 0x00: case 0xff:
					min_run = 6;
					out_fill = 0x00;
					if (unshifted) *unshifted &= (run_byte == out_fill);
					continue;

				// A1 or clock, with C2 (IAM) equivalent enough
				case 0xa1: case 0x14: case 0xc2:
					min_run = max_run = 3;
					out_fill = 0xa1;
					if (unshifted) *unshifted &= (run_byte == out_fill);
					continue;
			}

			// Invalid run byte, so no point in gathering more
			break;
		}

		// Stop if we hit a non-matching byte
		else if (b != run_byte)
		{
			// Undo the non-matching byte so we read it next time
			SetBitPos(_bitpos - 8);
			break;
		}
	}

	// Is it a run within the required length range?
	if (len && (!min_run || len >= min_run) && (!max_run || len <= max_run))
	{
		out_len = len;
		return true;
	}

	// Seek back so we've only consumed 1 bit
	SetBitPos(next_pos);

	// Ignore splice caused by a partial byte at the end of the stream
	if (IsWrapped())
	{
		out_len = -1;
		return false;
	}

	// Return the byte containing the splice bit
	out_fill = run_byte;
	out_len = 0;
	return true;
}

// Return next address mark, checking both data and clock patterns
// Returns FC for IAM, FE for IDAM, FB for DAM, F8 for DDAM, 00 for IDAM or DAM clock, 06 for DDAM clock, FF for no AM
uint8_t TrackDataParser::FindAM (int limit)
{
	_wrapped = false;

	for (limit *= 8; limit > 0 && !_wrapped; --limit)
	{
		// Determine next bit position
		auto next_pos = GetBitPos() + 1;

		// Read a bitstream byte
		auto b = ReadByte();

		// A1, C2, or their MFM clock with missing bit(s)?
		if (b == 0xa1 || b == 0xc2 || b == 0x14)
		{
			// Ensure the following 2 bytes match
			if (ReadByte() == b && ReadByte() == b)
			{
				// Read the address mark, which is only meaningful if b == 0xa1
				auto am = ReadByte();

				// Possible IDAM/DAM/DDAM sync?
				if (b == 0xa1)
				{
					// IDAM, DAM, or DDAM?
					if (am == 0xfe || am == 0xfb || am == 0xf8)
						return am;
				}
				// Possible IAM sync?
				else if (b == 0xc2)
				{
					// IAM?
					if (am == 0xfc)
						return am;
				}
				// Clock?
				else if (b == 0x14)
				{
					// Strip bit 0 as its value depends on bit 7 of the following byte
					am &= 0xfe;

					// IDAM/DAM or DDAM?
					if (am == 0x00 || am == 0x06)
						return am;
				}
			}
		}

		// Advance one bit from the start of the previous sync position
		SetBitPos(next_pos);
	}

	// No AM found within limit
	return 0xff;
}
