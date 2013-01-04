#ifndef FFSEQUENCE_H
#define FFSEQUENCE_H

#define AV_LOG_VERBOSE
// Needed by FFMPEG to avoid "error: ‘UINT64_C’ was not declared in this scope"
#define                           __STDC_CONSTANT_MACROS
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <stdexcept>
#include <vector>

#include "OpenImageIO/imageio.h"
OIIO_NAMESPACE_USING

// The following correspond to the data channels. Defined here for readability.
// Do not change these values as they are used in indexing fixed memory
// arrays.
#define FF_Y                                                0
#define FF_CB                                               1
#define FF_CR                                               2
#define FF_COMBINED                                         3

#define FF_FIRST_FRAME                                      1

#define FF_NO_FRAME                                        -1
#define FF_NO_STREAM                                       -1
#define FF_NO_DIMENSION                                    -1


class ffInterpolator
{
public:
    enum Type
    {
        Nearest, Linear, Cubic, Prefilter
    };
};

class ffAVPacket : public AVPacket
{
public:
    ffAVPacket():AVPacket() {}
};

class ffError : public std::runtime_error
{
public:
    enum FFError
    {
        ERROR_NO_VIDEO_STREAM,
        ERROR_BAD_FORMAT,
        ERROR_NO_DECODER,
        ERROR_ALLOC_ERROR,
        ERROR_BAD_FILENAME,
        ERROR_NULL_FILENAME
    };

private:
    int                                 m_ffError;
public:
    ffError(const std::string& what_arg, int error) :
        runtime_error(what_arg), m_ffError(error) {}
    int getError(void) { return m_ffError; }
};

class ffmpegError : public ffError
{
public:
    ffmpegError(const std::string& what_arg, int error) :
        ffError(what_arg, error) {}
};

class ffSize
{
public:
    ffSize(long w, long h): m_width(w), m_height(h) { }
    long                                    m_width;
    long                                    m_height;
};

class ffSizeRatio
{
public:
    ffSizeRatio(ffSize, ffSize);
    float                                   m_widthRatio;
    float                                   m_heightRatio;
};

class ffRawFrame
{
private:

public:
    enum PlaneType
    {
        Y = FF_Y, Cb = FF_CB, Cr = FF_CR, Combined = FF_COMBINED
    };

    unsigned char                  *m_pY;
    unsigned char                  *m_pCb;
    unsigned char                  *m_pCr;

    ffRawFrame(AVFrame*);
    ~ffRawFrame();

    void scalePlane(ffRawFrame::PlaneType, ffSize, ffSizeRatio,
                    ffInterpolator::Type);
};

class ffRawFrameFloat
{
private:
public:
    float                          *m_pfY;
    float                          *m_pfCb;
    float                          *m_pfCr;

    ffRawFrameFloat(long);
    ~ffRawFrameFloat();
};

class ffSequence
{
public:
    enum ffSequenceState
    {
        isLoading,
        isValid,
        isInvalid,
        justLoading,
        justOpened,
        justClosed,
        justErrored
    };

private:
    AVFormatContext                        *m_pFormatCtx;
    AVCodecContext                         *m_pCodecCtx;
    AVCodec                                *m_pCodec;

    long                                    m_totalFrames;
    long                                    m_currentFrame;
    ffSize                                  m_lumaSize;
    ffSize                                  m_chromaSize;
    ffSize                                  m_scaledSize;
    int                                     m_stream;
    static bool                             m_isInitialized;
    std::vector<ffRawFrame*>                m_frames;
    std::vector<ffRawFrameFloat*>           m_framesFloat;
    ffSequenceState                         m_state;

    std::string                             m_fileURI;

    void initialize(void);
    void pushRawFrame(AVFrame*);
    void freeRawFrames(void);
    void cleanup(void);
public:
    ffSequence(void);
    ~ffSequence();

    void readFile(char *fileName);
    void writeFile(char *, long, long);
    void closeFile(void);

    ffRawFrame* getRawFrame(long);
    ffRawFrame* setCurrentFrame(long);
    long getCurrentFrame(void);
    long getTotalFrames(void);
    ffSize getLumaSize(void);
    ffSize getChromaSize(void);
    ffSequenceState getState(void);
    std::string getFileURI(void);

    // The following virtual functions are provided for GUI applications to
    // provide a simple way to keep the UI in synchronicity with the object.
    virtual void onProgressStart(void);
    virtual void onProgress(double);
    virtual void onProgressEnd(void);
    virtual void onJustLoading(void);
    virtual void onJustOpened(void);
    virtual void onJustClosed(void);
    virtual void onJustErrored(void);
};
#endif // FFSEQUENCE_H
