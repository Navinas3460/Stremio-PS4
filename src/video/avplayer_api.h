// sceAvPlayer declarations for OpenOrbis.
//
// OpenOrbis' include/orbis/AvPlayer.h only declares `void f()` stubs, so the
// real types/signatures are declared here. They follow the PS Vita SDK's
// documented SceAvPlayer API (vitasdk: psp2/avplayer.h), which the PS4 library
// mirrors. *** Every item marked VERIFY is unconfirmed on PS4 hardware. ***
// If something misbehaves, compare against a known-working PS4 homebrew that
// uses libSceAvPlayer before touching the player logic.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *SceAvPlayerHandle;

// Memory callbacks. "Frame" allocations hold decoded video and must be
// GPU-visible direct memory.                                     VERIFY
typedef void *(*SceAvPlayerAllocate)(void *argP, uint32_t argAlignment, uint32_t argSize);
typedef void (*SceAvPlayerDeallocate)(void *argP, void *argMemory);
typedef void *(*SceAvPlayerAllocateTexture)(void *argP, uint32_t argAlignment, uint32_t argSize);
typedef void (*SceAvPlayerDeallocateTexture)(void *argP, void *argMemory);

typedef struct SceAvPlayerMemAllocator {
	void *objectPointer;
	SceAvPlayerAllocate allocate;
	SceAvPlayerDeallocate deallocate;
	SceAvPlayerAllocateTexture allocateTexture;
	SceAvPlayerDeallocateTexture deallocateTexture;
} SceAvPlayerMemAllocator;

// File replacement: all NULL = AvPlayer's own file I/O. AddSource with an
// https URL fails (0x806A0002) on PS4 FW 11.02 (confirmed on hardware), so
// network streams go through these callbacks backed by HTTP Range requests.
// `readOffset` returns the number of bytes read, or < 0 on error.   VERIFY
typedef int (*SceAvPlayerOpenFile)(void *p, const char *argFilename);
typedef int (*SceAvPlayerCloseFile)(void *p);
typedef int (*SceAvPlayerReadOffsetFile)(void *p, uint8_t *argBuffer, uint64_t argPosition, uint32_t argLength);
typedef uint64_t (*SceAvPlayerSizeFile)(void *p);

typedef struct SceAvPlayerFileReplacement {
	void *objectPointer;
	SceAvPlayerOpenFile open;
	SceAvPlayerCloseFile close;
	SceAvPlayerReadOffsetFile readOffset;
	SceAvPlayerSizeFile size;
} SceAvPlayerFileReplacement;

typedef void (*SceAvPlayerEventCallback)(void *p, int32_t argEventId, int32_t argSourceId, void *argEventData);

typedef struct SceAvPlayerEventReplacement {
	void *objectPointer;
	SceAvPlayerEventCallback eventCallback;
} SceAvPlayerEventReplacement;

typedef struct SceAvPlayerInitData { // sceAvPlayerInit accepted this on FW 11.02
	SceAvPlayerMemAllocator memoryReplacement;
	SceAvPlayerFileReplacement fileReplacement;
	SceAvPlayerEventReplacement eventReplacement;
	int32_t debugLevel;
	uint32_t basePriority;
	int32_t numOutputVideoFrameBuffers;
	uint8_t autoStart;
	uint8_t reserved[3];
	const char *defaultLanguage;
} SceAvPlayerInitData;

// Event ids passed to the event callback.                        VERIFY
enum {
	SCE_AVPLAYER_STATE_UNKNOWN = 0x00,
	SCE_AVPLAYER_STATE_STOP = 0x01,
	SCE_AVPLAYER_STATE_READY = 0x02,
	SCE_AVPLAYER_STATE_PLAY = 0x03,
	SCE_AVPLAYER_STATE_PAUSE = 0x04,
	SCE_AVPLAYER_STATE_BUFFERING = 0x05,
	SCE_AVPLAYER_TIMED_TEXT_DELIVERY = 0x10,
	SCE_AVPLAYER_WARNING_ID = 0x20,
	SCE_AVPLAYER_ENCRYPTION = 0x30,
};

enum {
	SCE_AVPLAYER_VIDEO = 0,
	SCE_AVPLAYER_AUDIO = 1,
	SCE_AVPLAYER_TIMEDTEXT = 2,
};

typedef struct SceAvPlayerAudio {
	uint16_t channelCount;
	uint8_t reserved[2];
	uint32_t sampleRate;
	uint32_t size;
	uint8_t languageCode[4];
} SceAvPlayerAudio;

typedef struct SceAvPlayerVideo {
	uint32_t width;
	uint32_t height;
	float aspectRatio;
	uint8_t languageCode[4];
} SceAvPlayerVideo;

typedef struct SceAvPlayerTextPosition {
	uint16_t top, left, bottom, right;
} SceAvPlayerTextPosition;

typedef struct SceAvPlayerTimedText {
	uint8_t languageCode[4];
	uint16_t textSize;
	uint16_t fontSize;
	SceAvPlayerTextPosition position;
} SceAvPlayerTimedText;

typedef union SceAvPlayerStreamDetails {
	uint8_t reserved[16];
	SceAvPlayerAudio audio;
	SceAvPlayerVideo video;
	SceAvPlayerTimedText subs;
} SceAvPlayerStreamDetails;

// Decoded video is NV12: Y plane (width x height) followed by the interleaved
// UV plane (width x height/2). Rows may be padded to a pitch.     VERIFY
typedef struct SceAvPlayerFrameInfo {
	uint8_t *pData;
	uint8_t reserved[4];
	uint64_t timeStamp; // milliseconds
	SceAvPlayerStreamDetails details;
} SceAvPlayerFrameInfo;

typedef struct SceAvPlayerStreamInfo {
	uint32_t type;
	uint8_t reserved[4];
	SceAvPlayerStreamDetails details;
	uint64_t duration;  // milliseconds
	uint64_t startTime;
} SceAvPlayerStreamInfo;

// Extended frame info (sceAvPlayerGetVideoDataEx). Layout matches the shadPS4
// emulator's reimplementation. Decoded NV12 frames have pitch =
// align(width, 64) and height = align(height, 16) (1080 -> 1088, confirmed on
// FW 11.02); the visible picture is given by the crop offsets.
typedef struct SceAvPlayerAudioEx {
	uint16_t channelCount;
	uint8_t reserved[2];
	uint32_t sampleRate;
	uint32_t size;
	uint8_t languageCode[4];
	uint8_t reserved1[64];
} SceAvPlayerAudioEx;

typedef struct SceAvPlayerVideoEx {
	uint32_t width;
	uint32_t height;
	float aspectRatio;
	uint8_t languageCode[4];
	uint32_t framerate;
	uint32_t cropLeftOffset;
	uint32_t cropRightOffset;
	uint32_t cropTopOffset;
	uint32_t cropBottomOffset;
	uint32_t pitch;
	uint8_t lumaBitDepth;
	uint8_t chromaBitDepth;
	uint8_t videoFullRangeFlag;
	uint8_t reserved1[37];
} SceAvPlayerVideoEx;

typedef union SceAvPlayerStreamDetailsEx {
	SceAvPlayerAudioEx audio;
	SceAvPlayerVideoEx video;
	uint8_t reserved1[80];
} SceAvPlayerStreamDetailsEx;

typedef struct SceAvPlayerFrameInfoEx {
	void *pData;
	uint8_t reserved[4];
	uint64_t timeStamp; // milliseconds
	SceAvPlayerStreamDetailsEx details;
} SceAvPlayerFrameInfoEx;

// sceAvPlayerAddSourceEx: lets AvPlayer play HLS (source type 8) as well as
// MP4 (1). Useful later with a streaming server that remuxes to HLS.
enum {
	SCE_AVPLAYER_SOURCE_TYPE_UNKNOWN = 0,
	SCE_AVPLAYER_SOURCE_TYPE_FILE_MP4 = 1,
	SCE_AVPLAYER_SOURCE_TYPE_HLS = 8,
};

typedef struct SceAvPlayerUri {
	const char *name;
	uint32_t length;
} SceAvPlayerUri;

typedef struct SceAvPlayerSourceDetails {
	SceAvPlayerUri uri;
	uint8_t reserved1[64];
	uint32_t sourceType;
	uint8_t reserved2[44];
} SceAvPlayerSourceDetails;

SceAvPlayerHandle sceAvPlayerInit(SceAvPlayerInitData *pInit);
int32_t sceAvPlayerAddSourceEx(SceAvPlayerHandle h, uint32_t argUriType, SceAvPlayerSourceDetails *argSourceDetails);
uint8_t sceAvPlayerGetVideoDataEx(SceAvPlayerHandle h, SceAvPlayerFrameInfoEx *videoInfo);
int32_t sceAvPlayerAddSource(SceAvPlayerHandle h, const char *argFilename);
int32_t sceAvPlayerStart(SceAvPlayerHandle h);
int32_t sceAvPlayerStop(SceAvPlayerHandle h);
int32_t sceAvPlayerPause(SceAvPlayerHandle h);
int32_t sceAvPlayerResume(SceAvPlayerHandle h);
int32_t sceAvPlayerClose(SceAvPlayerHandle h);
uint8_t sceAvPlayerIsActive(SceAvPlayerHandle h);
uint64_t sceAvPlayerCurrentTime(SceAvPlayerHandle h);
int32_t sceAvPlayerJumpToTime(SceAvPlayerHandle h, uint64_t argJumpTimeMsec);
int32_t sceAvPlayerStreamCount(SceAvPlayerHandle h);
int32_t sceAvPlayerGetStreamInfo(SceAvPlayerHandle h, uint32_t argStreamID, SceAvPlayerStreamInfo *argInfo);
uint8_t sceAvPlayerGetVideoData(SceAvPlayerHandle h, SceAvPlayerFrameInfo *videoInfo);
uint8_t sceAvPlayerGetAudioData(SceAvPlayerHandle h, SceAvPlayerFrameInfo *audioInfo);

#ifdef __cplusplus
}
#endif
