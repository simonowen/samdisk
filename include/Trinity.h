#ifndef TRINITY_H
#define TRINITY_H

class Trinity
{
	static const uint16_t TRINLOAD_UDP_PORT = 0xedb0;

public:
	Trinity ();
	~Trinity ();

	static std::unique_ptr<Trinity> Open ();
	const std::vector<std::string> devices () const;

	void cls ();
	void select_record (int record);
	std::vector<uint8_t> read_sector (int cyl, int head, int sector);
	std::vector<uint8_t> read_track (int cyl, int head);
	void send_file (const void *pv_, int len, int start_addr, int exec_addr);

private:
	int Send (const void *pv, int len);
	int Recv (void *pv, int len);

private:
	SOCKET m_socket = static_cast<SOCKET>(-1);
	sockaddr_in m_addr_from {};
	sockaddr_in m_addr_to {};
	std::vector<std::string> m_devices {};
};

#endif // TRINITY_H
