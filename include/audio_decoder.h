#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/time.h>
}
#include <spdlog/logger.h>

// 自定义删除器，用于智能指针管理
struct FormatContextDeleter {
    void operator()(AVFormatContext *ctx) {
        if (ctx) {
            avformat_close_input(&ctx);
        }
    }
};

struct CodecContextDeleter {
    void operator()(AVCodecContext *ctx) {
        if (ctx) {
            avcodec_free_context(&ctx);
        }
    }
};

// 解码器配置结构体
struct AudioDecoderConfig {
    size_t maxQueueSize = 100;        // 最大帧队列大小
    bool dropFramesWhenFull = false;  // 队列满时是否丢弃帧
    int timeoutMs = -1;               // 获取帧超时时间（毫秒），-1为无限等待
};

// 解码器错误枚举
enum class AudioDecoderError {
    SUCCESS,
    FILE_OPEN_ERROR,
    STREAM_INFO_ERROR,
    NO_AUDIO_STREAM,
    CODEC_NOT_FOUND,
    CODEC_CONTEXT_ALLOC_ERROR,
    CODEC_PARAMS_ERROR,
    CODEC_OPEN_ERROR
};

class AudioDecoder {
   public:
    // 使用类型别名定义智能指针
    using FormatContextPtr =
        std::unique_ptr<AVFormatContext, FormatContextDeleter>;
    using CodecContextPtr =
        std::unique_ptr<AVCodecContext, CodecContextDeleter>;

    explicit AudioDecoder(
        const AudioDecoderConfig &config = AudioDecoderConfig());
    ~AudioDecoder();

    // 文件操作
    AudioDecoderError open(const std::string &filename);
    void close();

    // 解码控制
    void start();
    void stop();
    void flush();

    // 帧操作
    bool getAudioFrame(AVFrame **frame, int timeout_ms = -1);
    size_t getQueueSize();

    // 设置与获取
    void setConfig(const AudioDecoderConfig &config);

    // 音频信息获取
    int getSampleRate() const;
    int getChannels() const;
    uint64_t getChannelLayout() const;
    AVSampleFormat getSampleFormat() const;
    double getDuration() const;
    double getCurrentTimestamp() const;
    AVRational getTimeBase() const;

    // 定位操作
    bool seek(double seconds);

   private:
    void cleanup();
    void decodeLoop();
    bool pushFrame(AVFrame *frame);

    // 配置
    AudioDecoderConfig config;

    // FFmpeg组件
    FormatContextPtr formatContext;
    CodecContextPtr codecContext;
    const AVCodec *codec{nullptr};
    int audioStreamIndex{-1};

    // 线程控制
    std::thread decoderThread;
    bool isDecoding{false};

    // 帧队列
    std::queue<AVFrame *> frameQueue;
    std::mutex frameQueueMutex;
    std::condition_variable frameAvailable;
    std::condition_variable queueNotFull;

    // 时间戳
    double currentPts{0.0};

    // 日志
    std::shared_ptr<spdlog::logger> _logger;
};
