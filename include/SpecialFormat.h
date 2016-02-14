#ifndef SPECIALFORMAT_H
#define SPECIALFORMAT_H

bool IsKBI19Track (const Track &track);
bool IsSystem24Track (const Track &track);
bool IsSpectrumSpeedlockTrack (const Track &track, int &random_offset);
bool IsCpcSpeedlockTrack (const Track &track, int &random_offset);
bool IsRainbowArtsTrack (const Track &track, int &random_offset);
bool IsKBI10Track (const Track &track);
bool IsLogoProfTrack (const Track &track);

#endif // SPECIALFORMAT_H
