#include "ffsequence.h"
#include <stdexcept>

//***********************
// * ffRawFrame
// **********************
ffRawFrame::ffRawFrame(AVFrame* pFrame) :
    m_pY(NULL), m_pCb(NULL), m_pCr(NULL)
{
    int y_width         = pFrame->width;
    int y_height        = pFrame->height;
    int c_hshift        = av_pix_fmt_descriptors[pFrame->format].log2_chroma_h;
    int c_wshift        = av_pix_fmt_descriptors[pFrame->format].log2_chroma_w;

    // Instead of making assumptions, and likely keeping this code more
    // flexibile, we do the correct approach according to FFMPEG and bit shift
    // the luma width and height.
    int c_width         = y_width >> c_hshift;
    int c_height        = y_height >> c_wshift;

    int y_stride        = pFrame->linesize[FF_Y];
    int cb_stride       = pFrame->linesize[FF_CB];
    int cr_stride       = pFrame->linesize[FF_CR];

    long len_y          = y_width * y_height;
    long len_cb         = c_width * c_height;
    long len_cr         = c_width * c_height;

    unsigned char* pY   = pFrame->data[FF_Y];
    unsigned char* pCb  = pFrame->data[FF_CB];
    unsigned char* pCr  = pFrame->data[FF_CR];

    // TODO: Check for null pointers and throw relevant exceptions.
    // Initialize the Y buffer.
    m_pY    = new unsigned char[len_y];
    for (long y = 0; y < y_height; y++)
        for (long x = 0; x < y_width; x++)
            m_pY[(y * y_width) + x] = pY[(y * y_stride) + x];

    // Initialize the Cb buffer.
    m_pCb   = new unsigned char[len_cb];
    for (long y = 0; y < c_height; y++)
        for (long x = 0; x < c_width; x++)
            m_pCb[(y * c_width) + x] = pCb[(y * cb_stride) + x];

    // Initialize the Cr buffer.
    m_pCr   = new unsigned char[len_cr];
    for (long y = 0; y < c_height; y++)
        for (long x = 0; x < c_width; x++)
            m_pCb[(y * c_width) + x] = pCr[(y * cr_stride) + x];
}

ffRawFrame::~ffRawFrame()
{
    delete[] m_pY;
    delete[] m_pCb;
    delete[] m_pCr;
}

ffRawFrameFloatScaled::ffRawFrameFloatScaled(ffRawFrame *pffRawFrame,
                                       ffSequence *pffSequence) :
    m_pfY(NULL),
    m_pfCb(NULL),
    m_pfCr(NULL)
{

}

ffRawFrameFloatScaled::~ffRawFrameFloatScaled()
{
    delete[] m_pfY;
    delete[] m_pfCb;
    delete[] m_pfCr;
}

//***********************
// * ffSequence
// **********************
bool ffSequence::m_isInitialized;

ffSequence::ffSequence(void):
    m_pFormatCtx(NULL), m_pCodecCtx(NULL), m_pCodec(NULL),
    m_totalFrames(FF_NO_FRAME), m_currentFrame(FF_NO_FRAME), m_lumaSize(0,0),
    m_chromaSize(0,0), m_stream(FF_NO_STREAM), m_isValid(false)
{
    if (!m_isInitialized)
        initialize();
}

ffSequence::~ffSequence()
{
    cleanup();
}

void ffSequence::initialize()
{
    av_register_all();
    m_isInitialized = true;
}

void ffSequence::pushRawFrame(AVFrame *pAVFrame)
{
    ffRawFrame* pRawFrame = new ffRawFrame(pAVFrame);
    m_frames.push_back(pRawFrame);
}

void ffSequence::cleanup(void)
{
    if (m_pCodecCtx)
    {
        avcodec_close(m_pCodecCtx);
        m_pCodecCtx = NULL;
    }

    if (m_pFormatCtx)
    {
        avformat_close_input(&m_pFormatCtx);
        m_pFormatCtx = NULL;
    }

    freeRawFrames();

    // The rest of the API relies on the various variables and makes
    // assumptions they are applicable. Zero them out.
    m_totalFrames = FF_NO_FRAME;
    m_currentFrame = FF_NO_FRAME;
    m_lumaSize = ffSize(FF_NO_DIMENSION, FF_NO_DIMENSION);
    m_chromaSize = ffSize(FF_NO_DIMENSION, FF_NO_DIMENSION);
    m_stream = FF_NO_STREAM;
    m_isValid = false;
}

void ffSequence::freeRawFrames(void)
{
    while(m_frames.size() > 0)
    {
        delete m_frames.back();
        m_frames.pop_back();
    }
}

void ffSequence::openFile(char *filename)
{
    try
    {
        cleanup();

        int retValue = -1;

        retValue = avformat_open_input(&m_pFormatCtx, filename, NULL, NULL);
        if (retValue < 0)
            throw ffmpegError("avformat_open_input() < 0", retValue);

        retValue = avformat_find_stream_info(m_pFormatCtx, NULL);
        if (retValue < 0)
            throw ffmpegError("avformat_find_stream_info() < 0", retValue);

        for(unsigned int i = 0; i < m_pFormatCtx->nb_streams; i++)
          if (m_pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                // TODO: Provide for multiple video streams via vector or like.
                m_stream = i;
                break;
            }
        if (m_stream < 0)
            throw ffError("m_stream < 0", FFERROR_NO_VIDEO_STREAM);

        m_pCodecCtx = m_pFormatCtx->streams[m_stream]->codec;
        if (m_pCodecCtx->pix_fmt != PIX_FMT_YUVJ420P)
            throw ffError("m_pCodecCtx->pix_fmt != PIX_FMT_YUVJ420P",
                          FFERROR_BAD_FORMAT);

        m_lumaSize = ffSize(m_pCodecCtx->width, m_pCodecCtx->height);
        // While we could simply assume that all 4:2:0 content is half width
        // and half height, there may be some discrepancy between the actual
        // stored width and height for the chroma channels. To avoid this, we
        // correctly bitshift according to the chroma shift values.
        m_chromaSize = ffSize(
                    m_lumaSize.m_width >>
                    av_pix_fmt_descriptors[m_pCodecCtx->pix_fmt].log2_chroma_w,
                    m_lumaSize.m_height >>
                    av_pix_fmt_descriptors[m_pCodecCtx->pix_fmt].log2_chroma_h);

        m_totalFrames = m_pFormatCtx->streams[m_stream]->nb_frames;

        m_pCodec = avcodec_find_decoder(m_pCodecCtx->codec_id);
        if (m_pCodec == NULL)
            throw ffError("m_pCodec == NULL", FFERROR_NO_DECODER);

        retValue = avcodec_open2(m_pCodecCtx, m_pCodec, NULL);
        if (retValue < 0)
            throw ffmpegError("avcodec_open2() < 0", retValue);

        m_frames.reserve(m_totalFrames);

        AVFrame *pTempFrame = avcodec_alloc_frame();
        if (pTempFrame == NULL)
            throw ffError("avcodec_alloc_frame() == NULL", FFERROR_ALLOC_ERROR);

        ffAVPacket    tempPacket;
        int         got_picture = -1;
        // Primary loop to iterate over the frames, allocate our ffRawFrames,
        // and push them into the vector storage.
        while (retValue = av_read_frame(m_pFormatCtx, &tempPacket), retValue == 0)
        {
            if (tempPacket.stream_index == m_stream)
            {
                int innerError = 0;
                innerError = avcodec_decode_video2(m_pCodecCtx, pTempFrame,
                                                   &got_picture, &tempPacket);
                if (innerError < 0)
                    throw ffmpegError("avcodec_decode_video2()", innerError);

                if (got_picture)
                    pushRawFrame(pTempFrame);
            }

            av_free_packet(&tempPacket);
        }
        if ((retValue != AVERROR_EOF) && (retValue != 0))
            throw ffmpegError("av_read_frame()", retValue);

        // got_picture above will return 0 for every bi-directionally encoded
        // frame. We must flush the frames out that remain internally buffered.
        while (retValue = avcodec_decode_video2(m_pCodecCtx, pTempFrame, &got_picture,
                    &tempPacket), ((retValue != AVERROR_EOF) && (retValue >= 0) &&
                    (got_picture)))
            pushRawFrame(pTempFrame);

        av_free_packet(&tempPacket);

        if (pTempFrame)
            av_freep(pTempFrame);

        // Only if we make it this far is the ffSequence object valid.
        m_isValid = true;
        setCurrentFrame(FF_FIRST_FRAME);
    }
    catch(ffError ffErr)
    {
        cleanup();
        throw ffErr;
    }
}

ffRawFrame* ffSequence::getRawFrame(long frame)
{
    return m_frames.at(frame - 1);
}

// All frames internally are zero based, but everything above this layer
// of abstraction should talk in one based "reality" terms.
ffRawFrame* ffSequence::setCurrentFrame(long frame)
{
    if ((frame - 1) != m_currentFrame)
        m_currentFrame = (frame - 1);

    return getRawFrame(frame);
}

long ffSequence::getCurrentFrame(void)
{
    return (m_currentFrame + 1);
}

long ffSequence::getTotalFrames(void)
{
    return m_frames.size();
}

ffSize ffSequence::getLumaSize(void)
{
    return m_lumaSize;
}

ffSize ffSequence::getChromaSize(void)
{
    return m_chromaSize;
}

bool ffSequence::isValid(void)
{
    return m_isValid;
}

void ffSequence::scalePlane()
{

}
