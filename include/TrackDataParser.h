#ifndef DATAPARSER_H
#define DATAPARSER_H

class TrackDataParser
{
public:
	TrackDataParser(const uint8_t *buf, int len);

	bool IsWrapped () const { return _wrapped; }

	int GetBitPos () const { return _bitpos; }
	int GetBytePos () const { return _bitpos / 8; }

	void SetData (const uint8_t *buf, int len);
	void SetBitPos (int bitpos);
	void SetBytePos (int bytepos) { SetBitPos(bytepos * 8); }

	uint8_t ReadByte ();
	uint8_t ReadPrevByte ();

	int GetGapRun (int &out_len, uint8_t &out_fill, bool *unshifted = nullptr);
	uint8_t FindAM (int limit);

private:
	const uint8_t *_track_data = nullptr;
	int _track_len = 0;
	int _bitpos = 0;
	bool _wrapped = false;
};

#endif // DATAPARSER_H
