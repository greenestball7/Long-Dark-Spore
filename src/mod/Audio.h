// Small software mixer on its own thread (winmm waveOut), playing The Long Dark clips that the
// launcher converted into the cache. Spore's own audio is untouched. HOOK:tld_audio_out
#pragma once
#include <string>

namespace Audio {

bool Start();                                    // opens the output device and mixer thread
void Stop();
bool Load(int contentIndex, const std::wstring& wavPath);  // tld_content row -> clip
bool Has(int contentIndex);

void SetAmbience(int contentIndex, float volume);  // crossfades the looping ambience bed; -1 = silence
void PlayOneShot(int contentIndex, float volume);
void PlayMusic(int contentIndex, float volume);    // no-op while music is still playing
bool MusicPlaying();
void SetMaster(float volume);                       // fades everything (0 when out of covered stages)

}  // namespace Audio
