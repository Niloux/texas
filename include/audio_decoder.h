#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
}

#include "logger.h"

// 错误码枚举
enum class AudioDecoderError {
  SUCCESS = 0,
  FILE_OPEN_ERROR,
  STREAM_INFO_ERROR,
  NO_AUDIO_STREAM,
  CODEC_NOT_FOUND,
  CODEC_CONTEXT_ALLOC_ERROR,
  CODEC_PARAMS_ERROR,
  CODEC_OPEN_ERROR
};

// 解码器配置结构
struct AudioDecoderConfig {
  size_t maxQueueSize = 100;      // 最大队列大小
  size_t preBufferFrames = 10;    // 预缓冲帧数
  bool dropFramesWhenFull = true; // 队列满时是否丢弃帧
};

class AudioDecoder {
public:
  explicit AudioDecoder(
      const AudioDecoderConfig &config = AudioDecoderConfig());
  ~AudioDecoder();

  // 禁用拷贝构造和赋值
  AudioDecoder(const AudioDecoder &) = delete;
  AudioDecoder &operator=(const AudioDecoder &) = delete;

  AudioDecoderError open(const std::string &filename);
  void start();
  void stop();
  bool getAudioFrame(AVFrame **frame, int timeout_ms = -1);

  // 新增接口
  bool isRunning() const { return isDecoding; }
  size_t getQueueSize();
  void flush(); // 清空队列
  void setConfig(const AudioDecoderConfig &config);

  // 获取音频参数
  int getSampleRate() const;
  int getChannels() const;
  double getDuration() const;
  AVSampleFormat getSampleFormat() const;

private:
  void decodeLoop();
  void cleanup();
  bool pushFrame(AVFrame *frame);

private:
  struct FFmpegDeleter {
    void operator()(AVFormatContext *p) { avformat_close_input(&p); }
    void operator()(AVCodecContext *p) { avcodec_free_context(&p); }
  };

  std::unique_ptr<AVFormatContext, FFmpegDeleter> formatContext;
  std::unique_ptr<AVCodecContext, FFmpegDeleter> codecContext;
  const AVCodec *codec = nullptr;
  int audioStreamIndex = -1;

  std::atomic<bool> isDecoding{false};
  std::thread decoderThread;
  std::queue<AVFrame *> frameQueue;
  std::mutex frameQueueMutex;
  std::condition_variable frameAvailable;
  std::condition_variable queueNotFull;

  AudioDecoderConfig config;
  std::shared_ptr<spdlog::logger> _logger;
};