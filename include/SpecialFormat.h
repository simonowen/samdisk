#ifndef SPECIALFORMAT_H
#define SPECIALFORMAT_H

bool IsKBI19Track (const Track &track);
bool IsSystem24Track (const Track &track);
bool IsSpectrumSpeedlockTrack (const Track &track, int &random_offset);
bool IsCpcSpeedlockTrack (const Track &track, int &random_offset);
bool IsRainbowArtsTrack (const Track &track, int &random_offset);
bool IsKBI10Track (const Track &track);
bool IsLogoProfTrack (const Track &track);

TrackData GenerateKBI19Track (const CylHead &cylhead, const Track &track);
TrackData GenerateSpectrumSpeedlockTrack (const CylHead &cylhead, const Track &track, int weak_offset);
TrackData GenerateCpcSpeedlockTrack (const CylHead &cylhead, const Track &track, int weak_offset);
TrackData GenerateRainbowArtsTrack (const CylHead &cylhead, const Track &track);
TrackData GenerateKBI10Track (const CylHead &cylhead, const Track &track);
TrackData GenerateLogoProfTrack (const CylHead &cylhead, const Track &track);
TrackData GenerateSystem24Track (const CylHead &cylhead, const Track &track);

#endif // SPECIALFORMAT_H
