#pragma once

#include "TrackBuilder.h"

class BitstreamTrackBuilder final : public TrackBuilder
{
public:
    BitstreamTrackBuilder(DataRate datarate, Encoding encoding);

    int size() const;
    void setEncoding(Encoding encoding) override;
    void addRawBit(bool bit) override;
    void addCrc(int size);

    BitBuffer& buffer();
    DataRate datarate() const;
    Encoding encoding() const;

private:
    BitBuffer m_buffer;
};
