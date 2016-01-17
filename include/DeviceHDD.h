#ifndef DEVICEHDD_H
#define DEVICEHDD_H

class DeviceHDD final : public HDD
{
public:
	DeviceHDD ();
	DeviceHDD (const DeviceHDD &) = delete;
	DeviceHDD & operator= (const DeviceHDD &) = delete;
	~DeviceHDD ();

public:
	bool Open (const std::string &path) override;

// Overrides
public:
	bool SafetyCheck () override;
	bool Lock () override;
	void Unlock () override;

	std::vector<std::string> GetVolumeList () const override;

public:
	static bool IsRecognised (const std::string &path);
	static bool IsDeviceHDD (const std::string &path);
	static bool IsFileHDD (const std::string &path);

	static std::vector<std::string> GetDeviceList ();

// Helpers
protected:
	bool ReadIdentifyData (HANDLE h_, IDENTIFYDEVICE &pIdentify_);
	bool ReadMakeModelRevisionSerial (const std::string &path);

protected:
	HANDLE hdev;
	std::vector<std::pair<HANDLE, std::string>> lLockHandles {};
};

#endif // DEVICEHDD_H
