// RPM command

#include "SAMdisk.h"

bool DiskRpm (const std::string &path)
{
	auto disk = std::make_shared<Disk>();
	if (!ReadImage(path, disk))
		return false;

	// Default to using cyl 0 head 0, but allow the user to override it
	CylHead cylhead(opt.range.empty() ? 0 :
		opt.range.cyl_end + 1, opt.range.head_end);

	auto forever = opt.force && util::is_stdout_a_tty();

	// Display 5 revolutions, or run forever if forced
	for (auto i = 0; !g_fAbort && (forever || i < 5); ++i)
	{
		auto &track = disk->read_track(cylhead);

		if (!track.tracktime)
		{
			if (i == 0)
				throw util::exception("not available for this disk type");

			break;
		}

		auto time_us = track.tracktime;
		auto rpm = 60'000'000.0f / track.tracktime;

		if (!forever)
			util::cout << util::fmt("%6d us = %3.2f rpm\n", time_us, rpm);
		else
		{
			util::cout << util::fmt("\r%6d us = %3.2f rpm  (Ctrl-C to stop)", time_us, rpm);
			util::cout.screen->flush();
		}

		// Discard source data for a fresh read
		disk->unload();
	}

	return true;
}
