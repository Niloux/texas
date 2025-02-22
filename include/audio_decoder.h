#pragma once

#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
#include <libswscale/swscale.h>
}
#include "logger.h"

class AudioDecoder {
public:
  AudioDecoder();
  ~AudioDecoder();

  bool open(const std::string &filename); // 打开音频文件
  void start();                           // 启动解码线程
  void stop();                            // 停止解码线程

  bool getAudioFrame(AVFrame **frame); // 获取解码后的音频帧

private:
  void decodeLoop(); // 解码循环

private:
  AVFormatContext *formatContext = nullptr;
  AVCodecContext *codecContext = nullptr;
  const AVCodec *codec = nullptr;
  int audioStreamIndex = -1;
  std::atomic<bool> isDecoding;
  std::thread decoderThread;
  std::queue<AVFrame *> frameQueue;
  std::mutex frameQueueMutex;
  std::condition_variable frameAvailable;
  std::shared_ptr<spdlog::logger> _logger;
};
