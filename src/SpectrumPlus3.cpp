// Sinclair Spectrum +3 helper functions

#include "SAMdisk.h"

static bool MatchBlock (const uint8_t *pbInput_, const uint8_t *pbMatch_, const char* psczRules_)
{
	while (*psczRules_)
	{
		switch (*psczRules_++)
		{
			// Character must match
			case 'y':
				if (*pbInput_++ != *pbMatch_++)
					return false;
				break;

			// Character not expected to match
			case 'n':
				++pbInput_;
				++pbMatch_;
				break;

			// Skip optional input if matched
			case 'o':
				if (*pbInput_ == *pbMatch_++)
					++pbInput_;
				break;
		}
	}

	// Fully matched
	return true;
}

void FixPlus3BootChecksum (Data &data)
{
	// Start from zero
	auto sum = data[0x0f] = 0x00;

	// Add up all the bytes in the sector
	for (auto &b : data)
		sum += b;

	// Adjust the total so the LSB is 3 (for +3)
	data[0x0f] = 0x03 - sum;
}


// Fix issues with some +3 boot loaders when used in a 3.5" drive.
// Some loaders turn the floppy drive motor off and on, without allowing time for it to
// reach normal speed. Reading sectors too soon will return errors, which is a problem
// for those loaders not willing to retry. Here we recognise affected loaders and apply
// code patches to work around the issues.
bool FixPlus3BootLoader (std::shared_ptr<Disk> &disk)
{
	bool patched = false;

	// Attempt to read the standard +3 boot sector
	const Sector *sector = nullptr;
	if (!disk->find(Header(0, 0, 1, 2), sector) || sector->data_size() < 512)
		return false;

	// Create a working copy to modify
	Data data(sector->data_copy());

	// Check for Alkatraz signature and  AND #F8 ; OR #04  at specific position
	// Example disk: Artura
	if (!patched &&
		!std::memcmp(&data[0x145], "ALKATRAZ", 8) &&
		!std::memcmp(&data[0x2d], "\xe6\xf8\xf6\x04", 4))
	{
		data[0x2e] -= 0x08;	// Clear motor off bit in value written to BANK678 variable

		Message(msgFix, "corrected motor issue in Alkatraz boot loader");
		patched = true;
	}

	// Decrypter code - the data (HL) and length (BC) values will change between loaders, and the DI is optional
	static const std::vector<uint8_t> erbe_decrypt
	{
		0x21, 0x2b, 0xfe,	// LD HL,#FE2B
		0x01, 0x6c, 0x01,	// LD BC,#016C
		0x11, 0x10, 0xfe,	// LD DE,#FE10
		0x79,				// LD A,C
		0xf3,				// DI
		0xed, 0x4f,			// LD R,A
		0xed, 0x5f,			// loop: LD A,R
		0xae,				// XOR (HL)
		0xeb,				// EX DE,HL
		0xae,				// XOR (HL)
		0xeb,				// EX DE,HL
		0x77,				// LD (HL),A
		0x23,				// INC HL
		0x13,				// INC DE
		0x0b,				// DEC BC
		0x78,				// LD A,B
		0xb1,				// OR C
		0x20, 0xf2			// JR NZ,loop
	};

	// Matching rules against code block above
	static const char *pcszErbeMatch = "ynyynnyyyyoyyyyyyyyyyyyyyyy";
	int code_offset = 0x10;

	// Check for Erbe decrypter
	// Example disk: Barbarian II (Erbe)
	if (!patched && MatchBlock(&data[code_offset], erbe_decrypt.data(), pcszErbeMatch))
	{
		// Determine decryption parameters
		int data_offset = data[code_offset + 1];
		uint16_t len = (data[code_offset + 5] << 8) | data[code_offset + 4];	// Extract length from LD BC,nn instruction
		uint8_t r = data[code_offset + 4];										// R register initialised from LSB of length
		r = (r & 0x80) | ((r + 2) & 0x7f);										// R advances by 2 between LD R,A and LD A,R, with 7-bit update

		// Decrypt the block
		for (auto i = 0; i < len; ++i)
		{
			data[data_offset + i] ^= data[code_offset + i] ^ r;		// XOR source+dest+R to decrypt
			r = (r & 0x80) | ((r + 0x0d) & 0x7f);					// R advanced by 0x0d each loop, with 7-bit update
		}

		// Locate the motor-off instruction
		for (int i = data_offset + 3; i < data_offset + len - 2; ++i)
		{
			// Check for LD B,#1F ; LD A,#01 ; OUT (C),A
			if (data[i] != 0x01 || std::memcmp(&data[i - 3], "\x06\x1f\x3e\x01\xed\x79", 6))
				continue;

			// Patch the missing motor-on bit
			data[i] |= 0x08;

			// Replace the decrypter with code to turn the motor on and allow some spin-up time
			static std::vector<uint8_t> fix
			{
				0xf6, 0x08,		// OR #08
				0xed, 0x79,		// OUT (C),A
				0x2e, 0x02,		// LD L,#02
				0x0b,			// loop: DEC BC
				0x78,			// LD A,B
				0xb1,			// OR C
				0x20, 0xfb,		// JR NZ,loop
				0x2d,			// DEC L
				0x20, 0xf8,		// JR NZ,loop
				0x18, 0x00		// jr start [offset set below]
			};

			// Set the JR distance and apply the fix
			fix[fix.size() - 1] = static_cast<uint8_t>(data_offset - code_offset - fix.size());
			std::copy(fix.begin(), fix.end(), data.begin() + code_offset);

			Message(msgFix, "corrected motor issue in Erbe boot loader");
			patched = true;

			break;
		}
	}

	// The Ubi Soft loaders used by Iron Lord and Twin World are different enough to need separate matching
	// Example disk: Iron Lord
	if (!patched &&
		((!std::memcmp(&data[0x1f0], "IRONLORD", 8) && CRC16(&data[0x10], 0x1f0 - 0x10) == 0x3e0a) ||
		 (!std::memcmp(&data[0x180], "TWINWORLD", 9) && CRC16(&data[0x10], 0x180 - 0x10) == 0x5546)))
	{
		static std::vector<uint8_t> fix
		{
			0x31, 0x00, 0x5e,	// LD SP,#5E00
			0x11, 0x13, 0x0c,	// LD DE,#130C
			0xed, 0x51,			// OUT (C),D
			0x06, 0x7f,			// LD B,#7F
			0xed, 0x59,			// OUT (C),E
			0x2e, 0x02,			// LD L,#02
			0x0b,				// loop: DEC BC
			0x78,				// LD A,B
			0xb1,				// OR C
			0x20, 0xfb,			// JR NZ,loop
			0x2d,				// DEC L
			0x20, 0xf8,			// JR NZ,loop
			0x18, 0x00			// jr start [offset set below]
		};

		// Set the JR distance and apply the fix
		fix[fix.size() - 1] = (data[0x1f0] == 'I') ? 0x08 : 0x04;
		std::copy(fix.begin(), fix.end(), data.begin() + 0x10);

		Message(msgFix, "corrected motor issue in UbiSoft boot loader");
		patched = true;
	}

	// Infogrames loader
	// Example disk: North And South - Side A (1989)
	if (!patched &&
		!std::memcmp(&data[0x41], "\xaf\x21\x00\x58", 4) &&
		CRC16(&data[0x10], 0x053 - 0x10) == 0x6e53)
	{
		static const std::vector<uint8_t> fix
		{
			0x2e, 0x02,			// LD L,#02
			0x0b,				// loop: DEC BC
			0x78,				// LD A,B
			0xb1,				// OR C
			0x20, 0xfb,			// JR NZ,loop
			0x2d,				// DEC L
			0x20, 0xf8,			// JR NZ,loop
			0x00, 0x00, 0x00,	// NOP ; NOP ; NOP
			0x00, 0x00			// NOP ; NOP
		};

		// Apply the fix
		std::copy(fix.begin(), fix.end(), data.begin() + 0x41);

		Message(msgFix, "corrected motor issue in Infogrames boot loader");
		patched = true;
	}

	// Have we patched the sector?
	if (patched)
	{
		// Fix the +3 boot checksum
		FixPlus3BootChecksum(data);

		// Replace the original sector with our modified version
		CylHead cylhead(0, 0);
		auto t = disk->read_track(cylhead);
		t.find(sector->header)->datas().assign({ std::move(data) });
		disk->write(cylhead, std::move(t));

		return true;
	}


	// Read a potential second stage loader from the following track
	if (!disk->find(Header(1, 0, 193, 6), sector) || sector->data_size() < 6144)
		return false;

	data = sector->data_copy();

	// The second stage Shadow Warriors loader disables the drive motor after each sector read.
	// Example disk: Shadow Warriors
	if (!std::memcmp(&data[653], "\x49\x2a\xff\x02\x07\x00\x04", 4) &&
		CRC16(data.data(), 1024) == 0x4717)
	{
		data[659] = 0x0c;	// Change 0x04 to 0x0C to keep motor on

		CylHead cylhead(1, 0);
		auto t = disk->read_track(cylhead);
		t.find(sector->header)->datas().assign({ std::move(data) });
		disk->write(cylhead, std::move(t));

		Message(msgFix, "corrected motor issue in Shadow Warriors second-stage loader");
		return true;
	}

	// Nothing changed
	return false;
}
