// Added: Unity Fixture suite for the Babylon column's sound driver. It installs the adapter
// context in soundBabylonTestMain.cpp with a NullDevice and a NullClock, so voice positions and
// the platform clock move only when a test says so. The expected dB-to-gain and pan numbers come
// from DirectSound's hundredths-of-a-dB rules and the driver's own tables, which are the DX9
// driver's tables.
#include <unity_fixture.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include <babylon/audio/Handle.h>
#include <babylon/audio/NullDevice.h>
#include <babylon/time/NullClock.h>

#include <sound/AudioLib.h>
#include <sound/Driver.h>
#include <sound/DriverOutput.h>

#include <std/Babylon/stdBabylon.h>

TEST_GROUP(SoundDriverBabylon);

extern "C" uint32_t SoundDriver_TestGetVoiceIndex(const tSysSoundBuffer* pBuffer);
extern "C" uint32_t SoundDriver_TestGetVoiceGeneration(const tSysSoundBuffer* pBuffer);

namespace
{
    babylon::audio::NullDevice* pDevice  = nullptr;
    babylon::time::NullClock*   pClock   = nullptr;
    int                         numLogs  = 0;

    int J3DAPI TestLog(const char* pFormat, ...)
    {
        (void)pFormat;
        ++numLogs;
        return 0;
    }

    babylon::audio::VoiceHandle VoiceOf(const tSysSoundBuffer* pBuffer)
    {
        return babylon::audio::VoiceHandle{ SoundDriver_TestGetVoiceIndex(pBuffer),
            SoundDriver_TestGetVoiceGeneration(pBuffer) };
    }

    std::vector<uint8_t> MakePcm(size_t frames, size_t bytesPerSample, size_t channels)
    {
        return std::vector<uint8_t>(frames * bytesPerSample * channels, 0);
    }

    // DirectSound attenuation is in hundredths of a dB: gain = 10^(dB / 2000).
    float ExpectedGain(float volume)
    {
        return (float)std::pow(10.0, (double)SoundDriver_GetDecibelVolume(volume) / 2000.0);
    }

    float ExpectedPan(float pan)
    {
        return SoundDriver_GetPanFromTable(SoundDriver_GetIntPan(pan));
    }

    bool HasFlag(tSoundChannelFlag flags, tSoundChannelFlag flag)
    {
        return (flags & flag) != 0;
    }

    uint64_t PositionOf(const tSysSoundBuffer* pBuffer)
    {
        return (uint64_t)SoundDriver_GetCurrentPosition(const_cast<tSysSoundBuffer*>(pBuffer));
    }
}

TEST_SETUP(SoundDriverBabylon)
{
    StdBabylonContext& rContext = stdBabylon_RequireContext("SoundDriverBabylon");

    pDevice = dynamic_cast<babylon::audio::NullDevice*>(rContext.pAudioDevice);
    pClock  = dynamic_cast<babylon::time::NullClock*>(rContext.pClock);
    TEST_ASSERT_NOT_NULL(pDevice);
    TEST_ASSERT_NOT_NULL(pClock);

    // Added: each test starts with the default Null output list and the system default device.
    pDevice->setOutputDevices({ babylon::audio::OutputDevice{ "null", "Null output" } });
    SoundDriver_SetConfiguredOutputDevice(NULL);

    numLogs = 0;
    TEST_ASSERT_EQUAL_INT(1, SoundDriver_Open(0, 0, NULL, NULL, NULL, NULL, TestLog));
    TEST_ASSERT_EQUAL_INT(0, numLogs);

    // The master volume is driver state that survives a Close; each test starts at full volume.
    SoundDriver_SetMaxVolume(1.0f);
}

TEST_TEAR_DOWN(SoundDriverBabylon)
{
    SoundDriver_Close();
    TEST_ASSERT_EQUAL_INT(0, numLogs);

    pDevice = nullptr;
    pClock  = nullptr;
}

TEST(SoundDriverBabylon, CreateAndPlayStartsAndStopKeepsThePosition)
{
    std::vector<uint8_t> pcm = MakePcm(480, 2, 1);
    tSoundChannelFlag flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pBuffer = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, NULL);
    TEST_ASSERT_NOT_NULL(pBuffer);

    tSoundChannelFlag status = SoundDriver_GetStatusAndCaps(pBuffer);
    TEST_ASSERT_TRUE(HasFlag(status, SOUND_CHANNEL_PLAYING));
    TEST_ASSERT_TRUE(HasFlag(status, SOUND_CHANNEL_SWMIXING));
    TEST_ASSERT_TRUE(HasFlag(status, SOUND_CHANNEL_GLOBALFOCUS));
    TEST_ASSERT_FALSE(HasFlag(status, SOUND_CHANNEL_LOOP));

    pDevice->advanceFrames(10);
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pBuffer));

    // DirectSound Stop keeps the play cursor, so the paused voice does not advance.
    SoundDriver_Stop(pBuffer);
    TEST_ASSERT_FALSE(HasFlag(SoundDriver_GetStatusAndCaps(pBuffer), SOUND_CHANNEL_PLAYING));
    pDevice->advanceFrames(10);
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pBuffer));

    // Play resumes where Stop left it.
    SoundDriver_Play(pBuffer, 0);
    TEST_ASSERT_TRUE(HasFlag(SoundDriver_GetStatusAndCaps(pBuffer), SOUND_CHANNEL_PLAYING));
    pDevice->advanceFrames(5);
    TEST_ASSERT_EQUAL_UINT64(30u, PositionOf(pBuffer));

    SoundDriver_Release(pBuffer);
}

TEST(SoundDriverBabylon, NonLoopingSoundStartsOverWhenPlayedAfterTheEnd)
{
    std::vector<uint8_t> pcm = MakePcm(100, 2, 1);
    tSoundChannelFlag flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pBuffer = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, NULL);
    TEST_ASSERT_NOT_NULL(pBuffer);

    pDevice->advanceFrames(200);
    TEST_ASSERT_FALSE(HasFlag(SoundDriver_GetStatusAndCaps(pBuffer), SOUND_CHANNEL_PLAYING));
    TEST_ASSERT_EQUAL_UINT64(200u, PositionOf(pBuffer));

    // DirectSound starts at the beginning again once the cursor reached the end.
    SoundDriver_Play(pBuffer, 0);
    TEST_ASSERT_TRUE(HasFlag(SoundDriver_GetStatusAndCaps(pBuffer), SOUND_CHANNEL_PLAYING));
    pDevice->advanceFrames(5);
    TEST_ASSERT_EQUAL_UINT64(10u, PositionOf(pBuffer));

    SoundDriver_Release(pBuffer);
}

TEST(SoundDriverBabylon, LoopingSoundWrapsItsPositionAndKeepsPlaying)
{
    std::vector<uint8_t> pcm = MakePcm(100, 2, 1);
    tSoundChannelFlag flags = (tSoundChannelFlag)(SOUND_CHANNEL_PLAYING | SOUND_CHANNEL_LOOP);
    tSysSoundBuffer* pBuffer = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, NULL);
    TEST_ASSERT_NOT_NULL(pBuffer);

    pDevice->advanceFrames(250);
    tSoundChannelFlag status = SoundDriver_GetStatusAndCaps(pBuffer);
    TEST_ASSERT_TRUE(HasFlag(status, SOUND_CHANNEL_PLAYING));
    TEST_ASSERT_TRUE(HasFlag(status, SOUND_CHANNEL_LOOP));
    TEST_ASSERT_EQUAL_UINT64(100u, PositionOf(pBuffer));

    pDevice->advanceFrames(50);
    TEST_ASSERT_EQUAL_UINT64(0u, PositionOf(pBuffer));

    SoundDriver_Release(pBuffer);
}

TEST(SoundDriverBabylon, APlayedVoiceStartsAtTheRequestedPositionZero)
{
    std::vector<uint8_t> pcm = MakePcm(480, 2, 1);
    tSoundChannelFlag flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pBuffer = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, NULL);
    TEST_ASSERT_NOT_NULL(pBuffer);
    TEST_ASSERT_EQUAL_UINT64(0u, PositionOf(pBuffer));
    pDevice->advanceFrames(1);
    TEST_ASSERT_EQUAL_UINT64(2u, PositionOf(pBuffer));
    SoundDriver_Release(pBuffer);
}

TEST(SoundDriverBabylon, DuplicatesShareTheSoundButNotTheVoicePosition)
{
    std::vector<uint8_t> pcm = MakePcm(480, 2, 1);

    tSoundChannelFlag flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pOriginal = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, NULL);
    TEST_ASSERT_NOT_NULL(pOriginal);

    flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pDuplicate = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, pOriginal);
    TEST_ASSERT_NOT_NULL(pDuplicate);
    TEST_ASSERT_NOT_EQUAL(pOriginal, pDuplicate);

    pDevice->advanceFrames(10);
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pOriginal));
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pDuplicate));

    // Stopping one voice leaves the other playing.
    SoundDriver_Stop(pDuplicate);
    pDevice->advanceFrames(10);
    TEST_ASSERT_EQUAL_UINT64(40u, PositionOf(pOriginal));
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pDuplicate));

    SoundDriver_Play(pDuplicate, 0);
    pDevice->advanceFrames(5);
    TEST_ASSERT_EQUAL_UINT64(30u, PositionOf(pDuplicate));

    SoundDriver_Release(pDuplicate);
    SoundDriver_Release(pOriginal);
}

TEST(SoundDriverBabylon, ReleasingTheOriginalKeepsItsDuplicatePlaying)
{
    std::vector<uint8_t> pcm = MakePcm(480, 2, 1);

    tSoundChannelFlag flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pOriginal = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, NULL);
    TEST_ASSERT_NOT_NULL(pOriginal);
    flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pDuplicate = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, pOriginal);
    TEST_ASSERT_NOT_NULL(pDuplicate);

    pDevice->advanceFrames(5);
    SoundDriver_Release(pOriginal);

    pDevice->advanceFrames(5);
    TEST_ASSERT_TRUE(HasFlag(SoundDriver_GetStatusAndCaps(pDuplicate), SOUND_CHANNEL_PLAYING));
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pDuplicate));

    SoundDriver_Release(pDuplicate);
}

TEST(SoundDriverBabylon, ReleasingTheDuplicateKeepsTheOriginalPlaying)
{
    std::vector<uint8_t> pcm = MakePcm(480, 2, 1);

    tSoundChannelFlag flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pOriginal = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, NULL);
    TEST_ASSERT_NOT_NULL(pOriginal);
    flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pDuplicate = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, pOriginal);
    TEST_ASSERT_NOT_NULL(pDuplicate);

    pDevice->advanceFrames(5);
    SoundDriver_Release(pDuplicate);

    pDevice->advanceFrames(5);
    TEST_ASSERT_TRUE(HasFlag(SoundDriver_GetStatusAndCaps(pOriginal), SOUND_CHANNEL_PLAYING));
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pOriginal));

    SoundDriver_Release(pOriginal);
}

TEST(SoundDriverBabylon, VolumePanAndFrequencyRoundTripAndReachTheVoice)
{
    std::vector<uint8_t> pcm = MakePcm(480, 2, 1);
    tSoundChannelFlag flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pBuffer = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, NULL);
    TEST_ASSERT_NOT_NULL(pBuffer);
    const babylon::audio::VoiceHandle voice = VoiceOf(pBuffer);

    // Volume: the table quantizes the value, and the voice gets the exact linear gain the
    // DirectSound buffer would apply.
    // 0.5 -> index 127 -> -600 hundredths of a dB -> 10^(-0.3) = 0.5011872.
    SoundDriver_SetVolume(pBuffer, 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 127.0f / 255.0f, SoundDriver_GetVolume(pBuffer));
    std::optional<babylon::audio::VoiceMix> mix = pDevice->voiceMix(voice);
    TEST_ASSERT_TRUE(mix.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5011872f, mix->gain);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, ExpectedGain(0.5f), mix->gain);

    // 0.25 -> index 63 -> -1210 hundredths of a dB -> 10^(-0.605) = 0.2483131.
    SoundDriver_SetVolume(pBuffer, 0.25f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 63.0f / 255.0f, SoundDriver_GetVolume(pBuffer));
    mix = pDevice->voiceMix(voice);
    TEST_ASSERT_TRUE(mix.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.2483131f, mix->gain);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, ExpectedGain(0.25f), mix->gain);

    // 1.0 -> index 255 -> -10 hundredths of a dB -> 10^(-0.005) = 0.9885531.
    SoundDriver_SetVolume(pBuffer, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, SoundDriver_GetVolume(pBuffer));
    mix = pDevice->voiceMix(voice);
    TEST_ASSERT_TRUE(mix.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.9885531f, mix->gain);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, ExpectedGain(1.0f), mix->gain);

    // Pan: -1 is left, 0 center, 1 right, through the same pan table.
    SoundDriver_SetPan(pBuffer, 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, ExpectedPan(0.5f), SoundDriver_GetPan(pBuffer));
    mix = pDevice->voiceMix(voice);
    TEST_ASSERT_TRUE(mix.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, ExpectedPan(0.5f), mix->pan);

    SoundDriver_SetPan(pBuffer, -0.5f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, ExpectedPan(-0.5f), SoundDriver_GetPan(pBuffer));
    SoundDriver_SetPan(pBuffer, 0.0f);
    // The pan table has several equal center entries, so the DX9 getter answers the highest one.
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, ExpectedPan(0.0f), SoundDriver_GetPan(pBuffer));
    mix = pDevice->voiceMix(voice);
    TEST_ASSERT_TRUE(mix.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, ExpectedPan(0.0f), mix->pan);

    // Frequency starts at the sound's rate and becomes pitch = Hz / rate.
    TEST_ASSERT_EQUAL_FLOAT(48000.0f, SoundDriver_GetFrequency(pBuffer));
    SoundDriver_SetFrequency(pBuffer, 24000.0f);
    TEST_ASSERT_EQUAL_FLOAT(24000.0f, SoundDriver_GetFrequency(pBuffer));
    pDevice->advanceFrames(100);
    TEST_ASSERT_EQUAL_UINT64(100u, PositionOf(pBuffer));

    SoundDriver_Release(pBuffer);
}

TEST(SoundDriverBabylon, PitchClampsToTheBabylonRange)
{
    std::vector<uint8_t> pcm = MakePcm(480, 2, 1);
    tSoundChannelFlag flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pBuffer = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, NULL);
    TEST_ASSERT_NOT_NULL(pBuffer);

    // 1 Hz on a 48 kHz sound is far below 1/16; the voice plays at 1/16, so 160 output frames
    // consume 10 source frames (20 bytes).
    SoundDriver_SetFrequency(pBuffer, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, SoundDriver_GetFrequency(pBuffer));
    pDevice->advanceFrames(160);
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pBuffer));

    SoundDriver_Release(pBuffer);
}

TEST(SoundDriverBabylon, MasterVolumeBecomesTheDeviceMasterGain)
{
    SoundDriver_SetMaxVolume(0.5f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, ExpectedGain(0.5f), pDevice->masterGain());

    std::vector<uint8_t> pcm = MakePcm(480, 2, 1);
    tSoundChannelFlag flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pBuffer = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, NULL);
    TEST_ASSERT_NOT_NULL(pBuffer);

    // The Sound layer passes channelVolume * maxVolume to SetVolume; the voice gain is compensated
    // so the effective gain stays the DX9 buffer gain.
    SoundDriver_SetVolume(pBuffer, 0.5f);
    const std::optional<babylon::audio::VoiceMix> mix = pDevice->voiceMix(VoiceOf(pBuffer));
    TEST_ASSERT_TRUE(mix.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, ExpectedGain(0.5f), mix->gain);

    SoundDriver_Release(pBuffer);
    SoundDriver_SetMaxVolume(1.0f);
}

TEST(SoundDriverBabylon, DeviceLossReopensAndVoicesSurvive)
{
    std::vector<uint8_t> pcm = MakePcm(480, 2, 1);
    tSoundChannelFlag flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pBuffer = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, NULL);
    TEST_ASSERT_NOT_NULL(pBuffer);

    pDevice->advanceFrames(10);
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pBuffer));

    pDevice->loseDevice();
    TEST_ASSERT_FALSE(pDevice->isOpen());
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pBuffer));

    // The per-update poll consumes DeviceLost and opens the default output again.
    SoundDriver_ListenerCommitDeferred();
    TEST_ASSERT_TRUE(pDevice->isOpen());
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pBuffer));

    pDevice->advanceFrames(5);
    TEST_ASSERT_EQUAL_UINT64(30u, PositionOf(pBuffer));

    SoundDriver_Release(pBuffer);
}

TEST(SoundDriverBabylon, WithoutADeviceEveryCallStillWorksSilently)
{
    StdBabylonContext& rContext = stdBabylon_RequireContext("SoundDriverBabylon");
    babylon::audio::Device* pSavedDevice = rContext.pAudioDevice;

    rContext.pAudioDevice = nullptr;
    numLogs = 0;
    TEST_ASSERT_EQUAL_INT(1, SoundDriver_Open(0, 0, NULL, NULL, NULL, NULL, TestLog));
    TEST_ASSERT_EQUAL_INT(1, numLogs);

    std::vector<uint8_t> pcm = MakePcm(480, 2, 1);
    tSoundChannelFlag flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pBuffer = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, NULL);
    TEST_ASSERT_NOT_NULL(pBuffer);
    TEST_ASSERT_EQUAL_UINT64(0u, PositionOf(pBuffer));

    SoundDriver_Play(pBuffer, 1);
    SoundDriver_Stop(pBuffer);
    SoundDriver_SetVolume(pBuffer, 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 127.0f / 255.0f, SoundDriver_GetVolume(pBuffer));

    tSoundChannelFlag status = SoundDriver_GetStatusAndCaps(pBuffer);
    TEST_ASSERT_FALSE(HasFlag(status, SOUND_CHANNEL_PLAYING));
    TEST_ASSERT_TRUE(HasFlag(status, SOUND_CHANNEL_SWMIXING));
    TEST_ASSERT_TRUE(HasFlag(status, SOUND_CHANNEL_GLOBALFOCUS));

    SoundDriver_Release(pBuffer);

    rContext.pAudioDevice = pSavedDevice;
    TEST_ASSERT_EQUAL_INT(1, SoundDriver_Open(0, 0, NULL, NULL, NULL, NULL, TestLog));
    TEST_ASSERT_EQUAL_INT(1, numLogs);

    // The teardown expects a quiet test after its own Close.
    numLogs = 0;
}

TEST(SoundDriverBabylon, CompressedSoundIsDecompressedBeforePlayback)
{
    std::vector<int16_t> samples(480);
    for ( size_t i = 0; i < samples.size(); ++i )
    {
        samples[i] = (int16_t)((int)(i * 37u) % 1000 - 500);
    }

    const uint32_t uncompressedSize = (uint32_t)(samples.size() * sizeof(int16_t));
    std::vector<uint8_t> compressed(samples.size() * sizeof(int16_t) * 3u + 256u);
    tAudioCompressorState compressor = { 0 };
    const int compressedSize = AudioLib_Compress(&compressor, compressed.data(),
        (const uint8_t*)samples.data(), (int)uncompressedSize, 1);
    TEST_ASSERT_GREATER_THAN_INT(0, compressedSize);

    std::vector<uint8_t> soundData(sizeof(uint32_t) + (size_t)compressedSize);
    std::memcpy(soundData.data(), &uncompressedSize, sizeof(uncompressedSize));
    std::memcpy(soundData.data() + sizeof(uncompressedSize), compressed.data(), (size_t)compressedSize);

    tSoundChannelFlag flags = (tSoundChannelFlag)(SOUND_CHANNEL_PLAYING | SOUND_CHANNEL_COMPRESSED);
    tSysSoundBuffer* pBuffer = SoundDriver_CreateAndPlay(48000, 16, 1, soundData.data(), soundData.size(), &flags, NULL);
    TEST_ASSERT_NOT_NULL(pBuffer);
    TEST_ASSERT_TRUE(HasFlag(SoundDriver_GetStatusAndCaps(pBuffer), SOUND_CHANNEL_PLAYING));

    pDevice->advanceFrames(10);
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pBuffer));

    SoundDriver_Release(pBuffer);
}

TEST(SoundDriverBabylon, OutputDeviceListIsCachedAndStringsAreTruncated)
{
    pDevice->setOutputDevices({ babylon::audio::OutputDevice{ "device-a", "Device A" },
        babylon::audio::OutputDevice{ "device-b-long", "Device B long name" } });

    TEST_ASSERT_EQUAL_size_t(2u, SoundDriver_GetNumOutputDevices());

    char aId[16]   = { 0 };
    char aName[8]  = { 0 };
    TEST_ASSERT_EQUAL_INT(1, SoundDriver_GetOutputDevice(0, aId, sizeof(aId), aName, sizeof(aName)));
    TEST_ASSERT_EQUAL_STRING("device-a", aId);
    TEST_ASSERT_EQUAL_STRING("Device ", aName);
    TEST_ASSERT_EQUAL_INT(0, SoundDriver_GetOutputDevice(2, aId, sizeof(aId), aName, sizeof(aName)));

    // The list read by GetNum is cached, so a later plug change does not shift the indices.
    pDevice->setOutputDevices({ babylon::audio::OutputDevice{ "device-a", "Device A" } });
    char aSecondId[16] = { 0 };
    TEST_ASSERT_EQUAL_INT(1, SoundDriver_GetOutputDevice(1, aSecondId, sizeof(aSecondId), NULL, 0));
    TEST_ASSERT_EQUAL_STRING("device-b-long", aSecondId);

    // A null buffer or zero size skips that string and still succeeds.
    TEST_ASSERT_EQUAL_INT(1, SoundDriver_GetOutputDevice(0, NULL, 0, NULL, 0));
}

TEST(SoundDriverBabylon, SetOutputDeviceSwitchesAndKeepsVoices)
{
    pDevice->setOutputDevices({ babylon::audio::OutputDevice{ "device-a", "Device A" },
        babylon::audio::OutputDevice{ "device-b", "Device B" } });
    TEST_ASSERT_EQUAL_INT(1, SoundDriver_SetOutputDevice("device-a"));
    TEST_ASSERT_EQUAL_STRING("device-a", SoundDriver_GetCurrentOutputDevice());

    std::vector<uint8_t> pcm = MakePcm(480, 2, 1);
    tSoundChannelFlag flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pBuffer = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, NULL);
    TEST_ASSERT_NOT_NULL(pBuffer);
    pDevice->advanceFrames(10);
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pBuffer));

    TEST_ASSERT_EQUAL_INT(1, SoundDriver_SetOutputDevice("device-b"));
    TEST_ASSERT_TRUE(pDevice->isOpen());
    TEST_ASSERT_EQUAL_STRING("device-b", SoundDriver_GetCurrentOutputDevice());
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pBuffer));
    pDevice->advanceFrames(5);
    TEST_ASSERT_EQUAL_UINT64(30u, PositionOf(pBuffer));

    // An unknown device is refused and the previous output stays in use.
    TEST_ASSERT_EQUAL_INT(0, SoundDriver_SetOutputDevice("missing"));
    TEST_ASSERT_TRUE(pDevice->isOpen());
    TEST_ASSERT_EQUAL_STRING("device-b", SoundDriver_GetCurrentOutputDevice());
    TEST_ASSERT_EQUAL_UINT64(30u, PositionOf(pBuffer));

    // Empty and NULL both select the system default.
    TEST_ASSERT_EQUAL_INT(1, SoundDriver_SetOutputDevice(NULL));
    TEST_ASSERT_EQUAL_STRING("", SoundDriver_GetCurrentOutputDevice());
    TEST_ASSERT_TRUE(pDevice->isOpen());

    SoundDriver_Release(pBuffer);
}

TEST(SoundDriverBabylon, ConfiguredDeviceMissingAtOpenUsesTheDefault)
{
    SoundDriver_Close();
    pDevice->setOutputDevices({ babylon::audio::OutputDevice{ "device-a", "Device A" } });
    SoundDriver_SetConfiguredOutputDevice("device-gone");

    numLogs = 0;
    TEST_ASSERT_EQUAL_INT(1, SoundDriver_Open(0, 0, NULL, NULL, NULL, NULL, TestLog));
    TEST_ASSERT_EQUAL_INT(1, numLogs);
    TEST_ASSERT_TRUE(pDevice->isOpen());
    TEST_ASSERT_EQUAL_STRING("", SoundDriver_GetCurrentOutputDevice());

    numLogs = 0;
}

TEST(SoundDriverBabylon, LossPrefersTheConfiguredDeviceAndSwitchesBackWhenItReturns)
{
    SoundDriver_Close();
    pDevice->setOutputDevices({ babylon::audio::OutputDevice{ "default", "Default" },
        babylon::audio::OutputDevice{ "device-b", "Device B" } });
    SoundDriver_SetConfiguredOutputDevice("device-b");

    numLogs = 0;
    TEST_ASSERT_EQUAL_INT(1, SoundDriver_Open(0, 0, NULL, NULL, NULL, NULL, TestLog));
    TEST_ASSERT_EQUAL_INT(0, numLogs);
    TEST_ASSERT_EQUAL_STRING("device-b", SoundDriver_GetCurrentOutputDevice());

    std::vector<uint8_t> pcm = MakePcm(480, 2, 1);
    tSoundChannelFlag flags = SOUND_CHANNEL_PLAYING;
    tSysSoundBuffer* pBuffer = SoundDriver_CreateAndPlay(48000, 16, 1, pcm.data(), pcm.size(), &flags, NULL);
    TEST_ASSERT_NOT_NULL(pBuffer);
    pDevice->advanceFrames(10);
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pBuffer));

    // While the configured device is available, a loss reopens it.
    pDevice->loseDevice();
    SoundDriver_ListenerCommitDeferred();
    TEST_ASSERT_TRUE(pDevice->isOpen());
    TEST_ASSERT_EQUAL_STRING("device-b", SoundDriver_GetCurrentOutputDevice());

    // When it is gone, the default opens instead and the voices keep their positions.
    pDevice->setOutputDevices({ babylon::audio::OutputDevice{ "default", "Default" } });
    pDevice->loseDevice();
    SoundDriver_ListenerCommitDeferred();
    TEST_ASSERT_TRUE(pDevice->isOpen());
    TEST_ASSERT_EQUAL_STRING("", SoundDriver_GetCurrentOutputDevice());
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pBuffer));

    // The device returns; the once-a-second check switches back to it.
    pDevice->setOutputDevices({ babylon::audio::OutputDevice{ "default", "Default" },
        babylon::audio::OutputDevice{ "device-b", "Device B" } });
    static_cast<void>(pClock->advance(std::chrono::milliseconds(1001)));
    SoundDriver_ListenerCommitDeferred();
    TEST_ASSERT_TRUE(pDevice->isOpen());
    TEST_ASSERT_EQUAL_STRING("device-b", SoundDriver_GetCurrentOutputDevice());
    TEST_ASSERT_EQUAL_UINT64(20u, PositionOf(pBuffer));

    SoundDriver_Release(pBuffer);
    numLogs = 0;
}

TEST_GROUP_RUNNER(SoundDriverBabylon)
{
    RUN_TEST_CASE(SoundDriverBabylon, CreateAndPlayStartsAndStopKeepsThePosition);
    RUN_TEST_CASE(SoundDriverBabylon, NonLoopingSoundStartsOverWhenPlayedAfterTheEnd);
    RUN_TEST_CASE(SoundDriverBabylon, LoopingSoundWrapsItsPositionAndKeepsPlaying);
    RUN_TEST_CASE(SoundDriverBabylon, APlayedVoiceStartsAtTheRequestedPositionZero);
    RUN_TEST_CASE(SoundDriverBabylon, DuplicatesShareTheSoundButNotTheVoicePosition);
    RUN_TEST_CASE(SoundDriverBabylon, ReleasingTheOriginalKeepsItsDuplicatePlaying);
    RUN_TEST_CASE(SoundDriverBabylon, ReleasingTheDuplicateKeepsTheOriginalPlaying);
    RUN_TEST_CASE(SoundDriverBabylon, VolumePanAndFrequencyRoundTripAndReachTheVoice);
    RUN_TEST_CASE(SoundDriverBabylon, PitchClampsToTheBabylonRange);
    RUN_TEST_CASE(SoundDriverBabylon, MasterVolumeBecomesTheDeviceMasterGain);
    RUN_TEST_CASE(SoundDriverBabylon, DeviceLossReopensAndVoicesSurvive);
    RUN_TEST_CASE(SoundDriverBabylon, WithoutADeviceEveryCallStillWorksSilently);
    RUN_TEST_CASE(SoundDriverBabylon, CompressedSoundIsDecompressedBeforePlayback);
    RUN_TEST_CASE(SoundDriverBabylon, OutputDeviceListIsCachedAndStringsAreTruncated);
    RUN_TEST_CASE(SoundDriverBabylon, SetOutputDeviceSwitchesAndKeepsVoices);
    RUN_TEST_CASE(SoundDriverBabylon, ConfiguredDeviceMissingAtOpenUsesTheDefault);
    RUN_TEST_CASE(SoundDriverBabylon, LossPrefersTheConfiguredDeviceAndSwitchesBackWhenItReturns);
}
