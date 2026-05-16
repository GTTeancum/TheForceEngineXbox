#include "gifWriter.h"

#ifdef _XBOX
// GIF recording not supported on Xbox (SSE2/intrin required by msf_gif).
namespace TFE_GIF
{
	bool startGif(const char*, u32, u32, u32) { return false; }
	void addFrame(const u8*) {}
	bool write() { return false; }
}
#else

#include <TFE_System/system.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include <assert.h>
#include <algorithm>
#include <vector>
#include <string>
#include <map>

#define MSF_GIF_IMPL
#include "msf_gif.h"

namespace TFE_GIF
{
	static MsfGifState s_gifState;
	static s32 s_centisecondsPerFrame;
	static s32 s_width;
	static s32 s_height;
	static char s_path[TFE_MAX_PATH];

	static std::vector<u8> s_tempBuffer;

	bool startGif(const char* path, u32 width, u32 height, u32 fps)
	{
		memset(&s_gifState, 0, sizeof(MsfGifState));
		msf_gif_begin(&s_gifState, width, height);

		s_width = width;
		s_height = height;

		s_centisecondsPerFrame = s32(100.0f/f32(fps) + 0.5f);
		strcpy(s_path, path);
		
		s_tempBuffer.resize(width * height * 4);
		
		return true;
	}

	void addFrame(const u8* imageData)
	{
		// We have to flip the frame vertically.
		u8* output = &s_tempBuffer[0];
		for (s32 y = 0; y < s_height; y++, output += s_width*4)
		{
			memcpy(output, &imageData[(s_height - y - 1) * s_width * 4], s_width * 4);
		}

		msf_gif_frame(&s_gifState, &s_tempBuffer[0], s_centisecondsPerFrame, 16, s_width * 4);
	}

	bool write()
	{
		MsfGifResult result = msf_gif_end(&s_gifState);
		
		FileStream file;
		if (!file.open(s_path, Stream::MODE_WRITE))
		{
			msf_gif_free(result);
			return false;
		}
		file.writeBuffer(result.data, (u32)result.dataSize);
		file.close();

		msf_gif_free(result);
		return true;
	}
}
#endif // _XBOX
