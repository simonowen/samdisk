// RPM command

#include "SAMdisk.h"

#if 0
bool FloppyRpm (const std::string &path)
{
	FLOPPY floppy;

	// Seek to cyl 0 for the measurement, unless given a specific cyl to use
	BYTE seek_cyl = (opt.range.cylto <= 0) ? 0 : opt.range.cylto + 1;

	if (!floppy.Open(path))
		return false;
	else if (!floppy.SetEncRate(FD_OPTION_MFM))
		return Error();
	else if (!floppy.CmdSeek(seek_cyl))
		return Error();

	// Loop until aborted, or after 5 iterations in non-forced mode
	for (int i = 0; !g_fAbort && (opt.force || i < 5); ++i)
	{
		DWORD dwTime = 0;

		if (!floppy.FdGetTrackTime(&dwTime) || !dwTime)
			return Error();

		DWORD dwRPMx100 = (DWORD)((60000000000ULL / dwTime) + 5) / 10;
		WriteCon("Drive motor = %3u.%02urpm%s%c", dwRPMx100 / 100, dwRPMx100 % 100,
				 (opt.force && is_a_tty()) ? "  (Ctrl-C to stop)" : "",
				 is_a_tty() ? '\r' : '\n');
	}

	floppy.Close();
	return true;
}
#endif
