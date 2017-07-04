#ifndef BITSTREAMTRACKBUFFER_H
#define BITSTREAMTRACKBUFFER_H

#include "TrackBuffer.h"

class BitstreamTrackBuffer final : public TrackBuffer
{
public:
	BitstreamTrackBuffer (DataRate datarate, Encoding encoding);

	void addBit (bool one) override;

	BitBuffer &buffer ();
	DataRate datarate () const;
	Encoding encoding () const;

private:
	BitBuffer m_buffer;
};

#endif // BITSTREAMTRACKBUFFER_H
