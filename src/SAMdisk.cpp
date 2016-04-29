// Main entry point and command-line handler

#include "SAMdisk.h"
#include "types.h"
#include "Disk.h"
#include "BlockDevice.h"

enum { cmdCopy, cmdScan, cmdFormat, cmdList, cmdView, cmdInfo, cmdDir, cmdRpm, cmdVerify, cmdUnformat, cmdVersion, cmdCreate, cmdEnd };

static const char* aszCommands[] =
{ "copy",  "scan",  "format",  "list",  "view",  "info",  "dir",  "rpm",  "verify",  "unformat",  "version",  "create",  nullptr };


OPTIONS opt;
volatile bool g_fAbort;

void Version ()
{
	util::cout << colour::WHITE << "SAMdisk 4.0 alpha (" __DATE__ ")" << colour::none << ", (c) 2016 Simon Owen\n";
}

int Usage ()
{
	Version();

	util::cout << "\n"
		<< " SAMDISK [copy|scan|format|list|view|info|dir|rpm] <args>\n"
		<< "\n"
		<< "  -c, --cyls=N        cylinder count (N) or range (A-B)\n"
		<< "  -h, --head=N        single head select (0 or 1)\n"
		<< "  -s, --sector[s]     sector count for format, or single sector select\n"
		<< "  -r, --retries=N     retry count for data errors (default=5)\n"
		<< "  -R, --rescan[s=N]   rescan track to locate faint sectors\n"
		<< "  -d, --double-step   step floppy head twice between tracks\n"
		<< "  -f, --force         suppress confirmation prompts (careful!)\n"
		<< "\n"
		<< "The following apply to regular disk formats only:\n"
		<< "  -n, --no-format     skip formatting stage when writing\n"
		<< "  -m, --minimal       read/write only used MGT tracks\n"
		<< "  -g, --gap3=N        override gap3 inter-sector spacing (default=auto)\n"
		<< "  -i, --interleave=N  override sector interleave (default=0)\n"
		<< "  -k, --skew=N        override inter-track skew (default=1)\n"
		<< "  -z, --size=N        override sector size code (default=2; 512 bytes)\n"
		<< "  -b, --base=N        override lowest sector number (default=1)\n"
		<< "  -0, --head[0|1]=N   override head 0 or 1 value\n"
		<< "\n"
		<< "See " << colour::CYAN << "http://simonowen.com/samdisk/" << colour::none << " for further details.\n";

	exit(1);
}


void ReportTypes ()
{
	util::cout << "\nSupported image types:\n";

	std::string header = " R/W:";
	for (auto p = aTypes; p->pszType; ++p)
	{
		if (p->pfnRead && p->pfnWrite && *p->pszType)
		{
			util::cout << header << ' ' << p->pszType;
			header.clear();
		}
	}

	header = "\n R/O:";
	for (auto p = aTypes; p->pszType; ++p)
	{
		if (p->pfnRead && !p->pfnWrite && *p->pszType)
		{
			util::cout << header << ' ' << p->pszType;
			header.clear();
		}
	}

	util::cout << '\n';
}



extern "C" {
#include "getopt_long.h"
}

enum { OPT_RPM = 256, OPT_RATE, OPT_LOG, OPT_VERSION, OPT_HEAD0, OPT_HEAD1, OPT_GAPMASK, OPT_MAXCOPIES, OPT_MAXSPLICE, OPT_CHECK8K, OPT_BYTES, OPT_HDF, OPT_ORDER };

struct option long_options[] =
{
	{ "cyls",		required_argument, nullptr, 'c' },
	{ "head",		required_argument, nullptr, 'h' },
	{ "sectors",	required_argument, nullptr, 's' },
	{ "retries",	required_argument, nullptr, 'r' },
	{ "rescans",	optional_argument, nullptr, 'R' },
	{ "double-step",	  no_argument, nullptr, 'd' },
	{ "verbose",		  no_argument, nullptr, 'v' },

	{ "no-format",		  no_argument, nullptr, 'n' },
	{ "minimal",		  no_argument, nullptr, 'm' },
	{ "gap3",		required_argument, nullptr, 'g' },
	{ "interleave",	required_argument, nullptr, 'i' },
	{ "skew",		required_argument, nullptr, 'k' },
	{ "size",		required_argument, nullptr, 'z' },
	{ "base",		required_argument, nullptr, 'b' },
	{ "head0",		required_argument, nullptr, '0' },
	{ "head1",		required_argument, nullptr, '1' },

	{ "hex",			  no_argument, nullptr, 'x' },
	{ "force",			  no_argument, nullptr, 'f' },
	{ "label",		required_argument, nullptr, 'L' },
	{ "data-copy",	required_argument, nullptr, 'D' },

	{ "debug",            no_argument, &opt.debug, 1 },
	{ "dec",              no_argument, &opt.hex, 0 },
	{ "hex-ish",		  no_argument, &opt.hex, 2 },
	{ "calibrate",		  no_argument, &opt.calibrate, 1 },
	{ "cpm",			  no_argument, &opt.cpm, 1 },
	{ "resize",			  no_argument, &opt.resize, 1 },
	{ "fm-overlap",		  no_argument, &opt.fmoverlap, 1 },
	{ "offsets",		  no_argument, &opt.offsets, 1 },
	{ "abs-offsets",	  no_argument, &opt.absoffsets, 1 },
	{ "no-write",		  no_argument, &opt.nowrite, 1 },
	{ "no-offsets",		  no_argument, &opt.offsets, 0 },
	{ "id-crc",			  no_argument, &opt.idcrc, 1 },
	{ "no-gap2",          no_argument, &opt.gap2, 0 },
	{ "no-gap4b",         no_argument, &opt.gap4b, 0 },
	{ "no-gaps",		  no_argument, &opt.gaps, GAPS_NONE },
	{ "gaps",		      no_argument, &opt.gaps, GAPS_CLEAN },
	{ "clean-gaps",	      no_argument, &opt.gaps, GAPS_CLEAN },
	{ "read-track",       no_argument, &opt.gaps, GAPS_TRACK },
	{ "all-gaps",         no_argument, &opt.gaps, GAPS_ALL },
	{ "gap2",             no_argument, &opt.gap2, 1 },
	{ "keep-overlap",	  no_argument, &opt.keepoverlap, 1 },
	{ "no-diff",		  no_argument, &opt.nodiff, 1 },
	{ "no-copies",		  no_argument, &opt.maxcopies, 1 },
	{ "no-duplicates",	  no_argument, &opt.nodups, 1 },
	{ "no-dups",	      no_argument, &opt.nodups, 1 },
	{ "no-check8k",       no_argument, &opt.check8k, 0 },
	{ "no-data",          no_argument, &opt.nodata, 1 },
	{ "no-wobble",		  no_argument, &opt.nowobble, 1 },
	{ "no-mt",			  no_argument, &opt.mt, 0 },
	{ "new-drive",		  no_argument, &opt.newdrive, 1 },
	{ "old-drive",		  no_argument, &opt.newdrive, 0 },
	{ "slow-step",		  no_argument, &opt.newdrive, 0 },
	{ "no-signature",	  no_argument, &opt.nosig, 1 },
	{ "no-zip",			  no_argument, &opt.nozip, 1 },
	{ "no-cfa",			  no_argument, &opt.nocfa, 1 },
	{ "no-identify",	  no_argument, &opt.noidentify, 1 },
	{ "byte-swap",		  no_argument, &opt.byteswap, 1 },
	{ "atom",			  no_argument, &opt.byteswap, 1 },
	{ "ace",			  no_argument, &opt.ace, 1 },
	{ "quick",			  no_argument, &opt.quick, 1 },
	{ "repair",			  no_argument, &opt.repair, 1},
	{ "fix",			  no_argument, &opt.fix, 1 },
	{ "no-fix",			  no_argument, &opt.fix, 0 },
	{ "fm",				  no_argument, &opt.fm, 1 },
	{ "no-fm",			  no_argument, &opt.fm, 0 },
	{ "blind",			  no_argument, &opt.blind, 1 },
//	{ "regular",		  no_argument, &opt.blind, 0 },			ToDo: restore?
	{ "no-weak",		  no_argument, &opt.noweak, 1 },
	{ "merge",			  no_argument, &opt.merge, 1 },
	{ "trim",             no_argument, &opt.trim, 1 },
	{ "flip",			  no_argument, &opt.flip, 1 },
	{ "legacy",			  no_argument, &opt.legacy, 1 },
//	{ "quiet",			  no_argument, nullptr, 'q' },
	{ "verify",			  no_argument, &opt.verify, 1 },
	{ "time",			  no_argument, &opt.time, 1 },			// undocumented
	{ "tty",			  no_argument, &opt.tty, 1 },
	{ "help",			  no_argument, nullptr, 0 },

	{ "log",		optional_argument, nullptr, OPT_LOG },
	{ "gap-mask",	required_argument, nullptr, OPT_GAPMASK },
	{ "max-copies", required_argument, nullptr, OPT_MAXCOPIES },
	{ "max-splice-bits",required_argument, nullptr, OPT_MAXSPLICE },
	{ "check8k",	optional_argument, nullptr, OPT_CHECK8K },
	{ "rate",		required_argument, nullptr, OPT_RATE },
	{ "rpm",		required_argument, nullptr, OPT_RPM },
	{ "bytes",		required_argument, nullptr, OPT_BYTES },
	{ "hdf",		required_argument, nullptr, OPT_HDF },
	{ "order",		required_argument, nullptr, OPT_ORDER },
	{ "version",		  no_argument, nullptr, OPT_VERSION },

	{ 0, 0, 0, 0 }
};

static char short_options[] = "?nmdvfLRxb:c:h:s:r:R:g:i:k:z:0:1:D:";

bool BadValue (const char *pcszName_)
{
	util::cout << "Invalid " << pcszName_ << " value '" << optarg << "'\n";
	return false;
}

bool ParseCommandLine (int argc_, char *argv_[])
{
	int arg;
	opterr = 1;

	while ((arg = getopt_long(argc_, argv_, short_options, long_options, nullptr)) != -1)
	{
		switch (arg)
		{
			case 'c':
				if (!GetRange(optarg, opt.range.cyl_begin, opt.range.cyl_end))
				{
					// -c0 is shorthand for -c0-0
					if (opt.range.cyls() == 0)
						opt.range.cyl_end = 1;
					else
					{
						util::cout << "Invalid cylinder count or range '" << optarg << "'\n";
						return false;
					}
				}
				break;

			case 'h':
			{
				int heads = 0;
				if (!GetInt(optarg, heads) || heads > MAX_DISK_HEADS)
				{
					util::cout << "Invalid head count or select '" << optarg << "'\n";
					return false;
				}

				opt.range.head_begin = (heads == 1) ? 1 : 0;
				opt.range.head_end = (heads == 0) ? 1 : 2;
				break;
			}

			case 's':
				if (!GetLong(optarg, opt.sectors))
					return BadValue("sector");
				break;

			case 'r':
				if (!GetInt(optarg, opt.retries))
					return BadValue("retries");
				break;

			case 'R':
				if (!optarg)
					opt.rescans = 1;
				else if (!GetInt(optarg, opt.rescans))
					return BadValue("rescans");
				break;

			case 'n':	opt.noformat = 1; break;
			case 'm':	opt.minimal = 1; break;

			case 'z':	if (!GetInt(optarg, opt.size)) return BadValue("size"); break;
			case 'g':	if (!GetInt(optarg, opt.gap3)) return BadValue("gap3"); break;
			case 'i':	if (!GetInt(optarg, opt.interleave)) return BadValue("sector interleave"); break;
			case 'k':	if (!GetInt(optarg, opt.skew)) return BadValue("track skew"); break;
			case 'b':	if (!GetInt(optarg, opt.base)) return BadValue("base-sector"); break;
			case '0':	if (!GetInt(optarg, opt.head0) || opt.head0 > 0xff) return BadValue("head0"); break;
			case '1':	if (!GetInt(optarg, opt.head1) || opt.head1 > 0xff) return BadValue("head1"); break;
			case 'D':	if (!GetInt(optarg, opt.datacopy)) return BadValue("data-copy"); break;

			case 'd':	opt.step = 2; break;
			case 'f':	++opt.force; break;
			case 'v':	++opt.verbose; break;
			case 'x':	opt.hex = 1; break;

			case 'L':	opt.label = optarg; break;

			case OPT_LOG:
				util::log.open(optarg ? optarg : "samdisk.log");
				if (util::log.bad())
				{
					util::cout << "failed to open log file for writing\n";
					return false;
				}
				util::cout.file = &util::log;
				break;

			case OPT_ORDER:
			{
				auto str = util::lowercase(optarg);
				if (str == std::string("cylinders").substr(0, str.length()))
					opt.cylsfirst = 1;
				else if (str == std::string("heads").substr(0, str.length()))
					opt.cylsfirst = 0;
				else
					return BadValue("order");
				break;
			}

			case OPT_RATE:
				if (!GetInt(optarg, opt.rate) ||
					(opt.rate != 250 && opt.rate != 300 && opt.rate != 500 && opt.rate != 1000))
				{
					return BadValue("rate");
				}
				break;

			case OPT_GAPMASK: if (!GetInt(optarg, opt.gapmask)) return BadValue("gap-mask"); break;
			case OPT_MAXCOPIES: if (!GetInt(optarg, opt.maxcopies) || opt.maxcopies < 1) return BadValue("max-copies"); break;
			case OPT_MAXSPLICE: if (!GetInt(optarg, opt.maxsplice)) return BadValue("max-splice-bits"); break;
			case OPT_CHECK8K: if (!optarg) opt.check8k = 1; else if (!GetInt(optarg, opt.check8k)) return BadValue("check8k"); break;
			case OPT_RPM:	if (!GetInt(optarg, opt.rpm) || (opt.rpm != 300 && opt.rpm != 360)) return BadValue("rpm"); break;
			case OPT_BYTES: if (!GetInt(optarg, opt.bytes) || !opt.bytes) return BadValue("bytes"); break;
			case OPT_HDF:	if (!GetInt(optarg, opt.hdf) || (opt.hdf != 10 && opt.hdf != 11)) return BadValue("hdf"); break;

			case OPT_VERSION:
				Version();
				ReportTypes();
#ifdef _WIN32
				ReportDriverVersion();
#endif
				return false;

			case ':':
			case '?':	// error
				util::cout << '\n';
				return false;

			// long option return
			case 0:
				break;
#ifdef _DEBUG
			default:
				return false;
#endif
		}
	}

	// Fail if there are no non-option arguments
	if (optind >= argc_)
	{
		if (!opt.verbose)
			Usage();

		// Allow -v to show the --version details
		Version();
		ReportTypes();
#ifdef _WIN32
		ReportDriverVersion();
#endif
		return false;
	}

	// The command is the first argument
	char *pszCommand = argv_[optind];

	// Match against known commands
	for (int i = 0; i < cmdEnd; ++i)
	{
		if (!strcasecmp(pszCommand, aszCommands[i]))
		{
			// Fail if a command has already been set
			if (opt.command)
				Usage();

			// Set the command and advance to the next argument position
			opt.command = i;
			++optind;
			break;
		}
	}

	if (opt.absoffsets) opt.offsets = 1;

	return true;
}


enum { argNone, argFloppy, argBlock, argHDD, argBootSector, argRecord, argUnknown, ARG_COUNT };

int GetArgType (const std::string &arg)
{
	if (arg.empty())
		return argNone;

	if (IsBootSector(arg))
		return argBootSector;

	if (IsRecord(arg))
		return argRecord;	// in most cases records are treated as disk images

	if (IsFloppy(arg))
		return argFloppy;

	if (BlockDevice::IsRecognised(arg))
		return argBlock;

	if (IsHddImage(arg))
		return argHDD;

	return argUnknown;
}

void signal_handler (int)
{
	g_fAbort = true;
}

int main (int argc_, char *argv_[])
{
	auto start_time = std::chrono::system_clock::now();

#ifdef _WIN32
#ifdef _DEBUG
	_CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_LEAK_CHECK_DF);
#endif

	SetUnhandledExceptionFilter(CrashDumpUnhandledExceptionFilter);

	if (!IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WIN2K), LOBYTE(_WIN32_WINNT_WIN2K), 0))
	{
		util::cout << "This program requires Windows 2000 or later.\n";
		return 1;
	}

	// Check if we've been run from GUI mode with no arguments
	if (!IsConsoleWindow() && util::is_stdout_a_tty() && argc_ == 1)
	{
		FreeConsole();
		MessageBox(nullptr, "I'm a console-mode utility, please run me from a Command Prompt!", "SAMdisk", MB_OK | MB_ICONSTOP);
		return 0;
	}
#endif // WIN32

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
#ifdef SIGBREAK
	signal(SIGBREAK, signal_handler);
#endif

	bool f = false;

	try
	{
		if (!ParseCommandLine(argc_, argv_))
			return 1;

		g_fAbort = false;

		// Read at most two non-option command-line arguments
		if (optind < argc_) strncpy(opt.szSource, argv_[optind++], arraysize(opt.szSource) - 1);
		if (optind < argc_) strncpy(opt.szTarget, argv_[optind++], arraysize(opt.szTarget) - 1);
		if (optind < argc_) Usage();

		int nSource = GetArgType(opt.szSource);
		int nTarget = GetArgType(opt.szTarget);

		switch (opt.command)
		{
			case cmdCopy:
			{
				if (nSource == argNone || nTarget == argNone)
					Usage();

				if (nSource == argUnknown && IsTrinity(opt.szTarget))
					f = Image2Trinity(opt.szSource, opt.szTarget);			// file/image -> Trinity
				else if ((nSource == argRecord || nSource == argBlock || nSource == argUnknown) && (nTarget == argRecord || nTarget == argUnknown || nTarget == argHDD /*for .raw*/))
					f = ImageToImage(opt.szSource, opt.szTarget);			// image -> image
				else if ((nSource == argHDD || nSource == argBlock) && nTarget == argHDD)
					f = Hdd2Hdd(opt.szSource, opt.szTarget);				// hdd -> hdd
				else if (nSource == argBootSector && nTarget == argUnknown)
					f = Hdd2Boot(opt.szSource, opt.szTarget);				// boot -> file
				else if (nSource == argUnknown && nTarget == argBootSector)
					f = Boot2Hdd(opt.szSource, opt.szTarget);				// file -> boot
				else if (nSource == argBootSector && nTarget == argBootSector)
					f = Boot2Boot(opt.szSource, opt.szTarget);				// boot -> boot
				else
					Usage();

				break;
			}

			case cmdList:
			{
				if (nTarget != argNone)
					Usage();

				if (nSource == argNone)
					f = ListDrives(opt.verbose);
				else if (nSource == argHDD || nSource == argBlock)
					f = ListRecords(opt.szSource);
				else if (nSource == argRecord || nSource == argUnknown)
					f = DirImage(opt.szSource);
				else
					Usage();

				break;
			}

			case cmdDir:
			{
				if (nSource == argNone || nTarget != argNone)
					Usage();
#if 0
				if (nSource == argFloppy)
					f = DirFloppy(opt.szSource);
				else
#endif
					if (nSource == argHDD)
						f = ListRecords(opt.szSource);
					else if (nSource == argRecord || nSource == argUnknown)
						f = DirImage(opt.szSource);
					else
						Usage();

					break;
			}

			case cmdScan:
			{
				if (nSource == argNone || nTarget != argNone)
					Usage();

				if (nSource == argRecord || nSource == argBlock || nSource == argUnknown)
					f = ScanImage(opt.szSource, opt.range);
				else
					Usage();

				break;
			}

			case cmdUnformat:
			{
				if (nSource == argNone || nTarget != argNone)
					Usage();

				// Don't write disk signatures during any formatting
				opt.nosig = true;

				if (nSource == argHDD)
					f = FormatHdd(opt.szSource);
#if 0
				else if (nSource == argRecord)
					f = FormatRecord(opt.szSource);
#endif
				else if (nSource == argUnknown)
					f = UnformatImage(opt.szSource, opt.range);
				else
					Usage();

				break;
			}

			case cmdFormat:
			case cmdVerify:
			{
				if (nSource == argNone || nTarget != argNone)
					Usage();
#if 0
				if (nSource == argFloppy)
				{
					// Set up the format for MGT
					FORMAT fmt = fmtMGT;
					fmt.gap3 = 0;	// auto gap3

					// Override some settings in ProDos CP/M mode
					if (opt.cpm)
					{
						fmt.sectors = fmtProDos.sectors;
						fmt.interleave = fmtProDos.interleave;
						fmt.skew = fmtProDos.skew;
						fmt.fill = fmtProDos.fill;
					}

					// To ensure it fits by default, halve the sector count in FM mode
					if (opt.fm == 1) fmt.sectors >>= 1;

					// Allow everything about the format to be overridden
					OverrideFormat(&fmt, true);

					// Check sector count and size
					ValidateGeometry(fmt);

					bool fFormat = opt.command == cmdFormat;
					bool fVerify = (opt.command == cmdVerify) || opt.verify;
					f = FormatVerifyFloppy(opt.szSource, opt.range, &fmt, fFormat, fVerify);
				}
				else
#endif
					if (opt.command == cmdFormat)
					{
						if (nSource == argHDD)
							FormatHdd(opt.szSource);
						else if (nSource == argBootSector)
							FormatBoot(opt.szSource);
						else if (nSource == argRecord)
							FormatRecord(opt.szSource);
						else
							Usage();
					}
					else
						Usage();

				break;
			}

			case cmdCreate:
			{
				if (nSource == argNone)
					Usage();

				if (nSource == argHDD && IsHddImage(opt.szSource) && (nTarget != argNone || opt.sectors != -1))
					f = CreateHddImage(opt.szSource, strtoul(opt.szTarget, nullptr, 0));
				else if (nSource == argUnknown && nTarget == argNone)
					f = CreateImage(opt.szSource, opt.range);
				else
					Usage();

				break;
			}

			case cmdInfo:
			{
				if (nSource == argNone || nTarget != argNone)
					Usage();

				if (nSource == argHDD || nSource == argBlock)
					f = HddInfo(opt.szSource, opt.verbose);
				else if (nSource == argRecord || nSource == argUnknown)
					f = ImageInfo(opt.szSource);
				else
					Usage();

				break;
			}

			case cmdView:
			{
				if (nSource == argNone || nTarget != argNone)
					Usage();

				if (nSource == argHDD || nSource == argBlock)
					f = ViewHdd(opt.szSource, opt.range);
				else if (nSource == argBootSector)
					f = ViewBoot(opt.szSource, opt.range);
				else if (nSource == argRecord || nSource == argUnknown)
					f = ViewImage(opt.szSource, opt.range);
				else
					Usage();

				break;
			}

			case cmdRpm:
			{
				if (nSource == argNone || nTarget != argNone)
					Usage();

				if (nSource == argRecord || nSource == argUnknown)
					f = DiskRpm(opt.szSource);
				else
					Usage();

				break;
			}

			case cmdVersion:
			{
				if (nSource != argNone || nTarget != argNone)
					Usage();

				Version();
				ReportTypes();
#ifdef _WIN32
				ReportDriverVersion();
#endif
				f = true;
				break;
			}

			default:
				Usage();
				break;
		}
	}
	catch (std::string &e)
	{
		util::cout << "Error: " << colour::RED << e << colour::none << '\n';
	}
	catch (std::exception &e)
	{
		util::cout << colour::RED << "Error: " << e.what() << colour::none << '\n';
	}
	catch (...)
	{
		util::cout << colour::RED << "Internal error!" << colour::none << '\n';
	}

	if (opt.time)
	{
		auto end_time = std::chrono::system_clock::now();
		auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
		util::cout << "Elapsed time: " << elapsed_ms << "ms\n";
	}

	util::log.close();

	return f ? 0 : 1;
}
