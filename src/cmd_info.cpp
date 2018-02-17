// Info command

#include "SAMdisk.h"

bool ImageInfo (const std::string &path)
{
	util::cout << "[" << path << "]\n";
	util::cout.screen->flush();

	auto disk = std::make_shared<Disk>();
	if (ReadImage(opt.szSource, disk))
	{
		const Format &fmt = disk->fmt;
		auto cyls = disk->cyls();
		auto heads = disk->heads();
		auto sectors = disk->fmt.sectors;

		util::cout << colour::cyan << " Type:   " << colour::none << disk->strType << "\n";
		assert(disk->strType != "<unknown>");

		if (sectors == 0)
			util::cout << colour::cyan << " Size:   " << colour::none <<
			util::fmt("%u Cyl%s, %u Head%s\n", cyls, cyls == 1 ? "" : "s", heads, heads == 1 ? "" : "s");
		else
		{
			util::cout << colour::cyan << " Format: " << colour::none <<
				util::fmt("%s %s, %2u cyls, %u heads, %2u sectors, %4u bytes/sector\n",
						  to_string(fmt.datarate).c_str(), to_string(fmt.encoding).c_str(),
						  disk->cyls(), disk->heads(), fmt.sectors, fmt.sector_size());
		}

		if (!disk->metadata.empty())
		{
			auto sep = "\n";

			size_t max_key_len{0};
			for (const auto &p : disk->metadata)
				max_key_len = std::max(p.first.size(), max_key_len);

			for (const auto &field : disk->metadata)
			{
				if (field.first != "comment" && !field.second.empty())
				{
					util::cout << sep;
					sep = "";

					util::cout << ' ' << colour::cyan << std::setw(max_key_len) << field.first <<
						colour::none << " : " << field.second << "\n";
				}
			}

			auto it = disk->metadata.find("comment");
			if (it != disk->metadata.end() && !it->second.empty())
				util::cout << sep << util::trim(it->second) << "\n";
		}

		return true;
	}

	return false;
}

bool HddInfo (const std::string &path, int verbosity)
{
	auto hdd = HDD::OpenDisk(path);

	if (!hdd)
		Error("open");
	else
	{
		util::cout << '[' << path << "]\n";
		ListDrive("", *hdd, verbosity + 1);	// automatically verbose

		if (verbosity > 0 && hdd->sIdentify.len)
		{
			util::cout << '\n';
			util::hex_dump(hdd->sIdentify.byte, hdd->sIdentify.byte + hdd->sIdentify.len);
		}

		return true;
	}

	return false;
}
