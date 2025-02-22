#include "audio_decoder.h"

namespace {
constexpr int ERROR_BUFFER_SIZE = 1024;

std::string getErrorString(int error) {
  char errbuf[ERROR_BUFFER_SIZE];
  av_strerror(error, errbuf, ERROR_BUFFER_SIZE);
  return std::string(errbuf);
}
} // namespace

AudioDecoder::AudioDecoder(const AudioDecoderConfig &config) : config(config) {
  _logger = Logger::getInstance().getLogger("AudioDecoder");
}

AudioDecoder::~AudioDecoder() {
  stop();
  cleanup();
}

void AudioDecoder::cleanup() {
  // 清理帧队列
  std::lock_guard<std::mutex> lock(frameQueueMutex);
  while (!frameQueue.empty()) {
    av_frame_free(&frameQueue.front());
    frameQueue.pop();
  }
}

AudioDecoderError AudioDecoder::open(const std::string &filename) {
  // 确保之前的资源被释放
  formatContext.reset();
  codecContext.reset();

  AVFormatContext *formatCtx = nullptr;
  int ret = avformat_open_input(&formatCtx, filename.c_str(), nullptr, nullptr);
  if (ret < 0) {
    _logger->error("Could not open file: {} - {}", filename,
                   getErrorString(ret));
    return AudioDecoderError::FILE_OPEN_ERROR;
  }
  formatContext.reset(formatCtx);

  if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
    _logger->error("Could not find stream information");
    return AudioDecoderError::STREAM_INFO_ERROR;
  }

  // 查找音频流
  audioStreamIndex = -1;
  for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
    if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audioStreamIndex = i;
      break;
    }
  }

  if (audioStreamIndex == -1) {
    _logger->error("Could not find audio stream");
    return AudioDecoderError::NO_AUDIO_STREAM;
  }

  // 获取解码器
  AVCodecParameters *codecParams =
      formatCtx->streams[audioStreamIndex]->codecpar;
  codec = avcodec_find_decoder(codecParams->codec_id);
  if (!codec) {
    _logger->error("Codec not found");
    return AudioDecoderError::CODEC_NOT_FOUND;
  }

  // 分配解码器上下文
  AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
  if (!codecCtx) {
    _logger->error("Could not allocate codec context");
    return AudioDecoderError::CODEC_CONTEXT_ALLOC_ERROR;
  }
  codecContext.reset(codecCtx);

  if (avcodec_parameters_to_context(codecCtx, codecParams) < 0) {
    _logger->error("Could not copy codec params to context");
    return AudioDecoderError::CODEC_PARAMS_ERROR;
  }

  // 设置解码器选项
  AVDictionary *opts = nullptr;
  av_dict_set(&opts, "strict", "experimental", 0); // 使用实验性功能

  // 打开解码器
  ret = avcodec_open2(codecCtx, codec, &opts);
  av_dict_free(&opts); // 释放字典，无论是否成功都需要释放

  if (ret < 0) {
    _logger->error("Could not open codec");
    return AudioDecoderError::CODEC_OPEN_ERROR;
  }

  // 设置时间基准
  codecCtx->pkt_timebase = formatCtx->streams[audioStreamIndex]->time_base;

  return AudioDecoderError::SUCCESS;
}

void AudioDecoder::start() {
  if (!isDecoding) {
    isDecoding = true;
    decoderThread = std::thread(&AudioDecoder::decodeLoop, this);
  }
}

void AudioDecoder::stop() {
  if (isDecoding) {
    isDecoding = false;
    frameAvailable.notify_all();
    queueNotFull.notify_all();
    if (decoderThread.joinable()) {
      decoderThread.join();
    }
  }
}

bool AudioDecoder::pushFrame(AVFrame *frame) {
  std::unique_lock<std::mutex> lock(frameQueueMutex);
  if (frameQueue.size() >= config.maxQueueSize) {
    if (config.dropFramesWhenFull) {
      _logger->warn("Frame queue full, dropping frame");
      av_frame_free(&frame);
      return false;
    }
    queueNotFull.wait(lock, [this]() {
      return frameQueue.size() < config.maxQueueSize || !isDecoding;
    });
  }

  if (!isDecoding) {
    av_frame_free(&frame);
    return false;
  }

  frameQueue.push(frame);
  frameAvailable.notify_one();
  return true;
}

bool AudioDecoder::getAudioFrame(AVFrame **frame, int timeout_ms) {
  std::unique_lock<std::mutex> lock(frameQueueMutex);

  bool success;
  if (timeout_ms < 0) {
    // 无限等待
    frameAvailable.wait(
        lock, [this]() { return !frameQueue.empty() || !isDecoding; });
    success = !frameQueue.empty();
  } else {
    // 带超时的等待
    success = frameAvailable.wait_for(
        lock, std::chrono::milliseconds(timeout_ms),
        [this]() { return !frameQueue.empty() || !isDecoding; });
  }

  if (!success || frameQueue.empty()) {
    return false;
  }

  *frame = frameQueue.front();
  frameQueue.pop();
  queueNotFull.notify_one();
  return true;
}

void AudioDecoder::decodeLoop() {
  AVPacket *packet = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();

  while (isDecoding) {
    int ret = av_read_frame(formatContext.get(), packet);
    if (ret < 0) {
      if (ret == AVERROR_EOF) {
        // 处理文件结束
        // 刷新解码器缓冲
        avcodec_send_packet(codecContext.get(), nullptr);
        _logger->info("End of file reached");
      } else {
        _logger->error("Error reading frame: {}", getErrorString(ret));
      }
      break;
    }

    if (packet->stream_index == audioStreamIndex) {
      // 添加时间戳处理
      if (packet->pts != AV_NOPTS_VALUE) {
        packet->pts = av_rescale_q(
            packet->pts, formatContext->streams[audioStreamIndex]->time_base,
            codecContext->time_base);
      }

      if (packet->dts != AV_NOPTS_VALUE) {
        packet->dts = av_rescale_q(
            packet->dts, formatContext->streams[audioStreamIndex]->time_base,
            codecContext->time_base);
      }

      if (avcodec_send_packet(codecContext.get(), packet) == 0) {
        while (true) {
          ret = avcodec_receive_frame(codecContext.get(), frame);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
          } else if (ret < 0) {
            _logger->error("Error during decoding");
            break;
          }

          // 确保时间戳有效
          if (frame->pts == AV_NOPTS_VALUE) {
            frame->pts = frame->best_effort_timestamp;
          }

          // 克隆帧并加入队列
          AVFrame *clone = av_frame_clone(frame);
          if (clone) {
            std::lock_guard<std::mutex> lock(frameQueueMutex);
            frameQueue.push(clone);
            frameAvailable.notify_one();
          }
        }
      }
    }
    av_packet_unref(packet);
  }

  // 清理资源
  av_frame_free(&frame);
  av_packet_free(&packet);
}

size_t AudioDecoder::getQueueSize() {
  std::lock_guard<std::mutex> lock(frameQueueMutex);
  return frameQueue.size();
}

void AudioDecoder::flush() {
  std::lock_guard<std::mutex> lock(frameQueueMutex);
  while (!frameQueue.empty()) {
    av_frame_free(&frameQueue.front());
    frameQueue.pop();
  }
}

void AudioDecoder::setConfig(const AudioDecoderConfig &newConfig) {
  std::lock_guard<std::mutex> lock(frameQueueMutex);
  config = newConfig;
}

int AudioDecoder::getSampleRate() const {
  return codecContext ? codecContext->sample_rate : 0;
}

int AudioDecoder::getChannels() const {
  if (!codecContext) {
    return 0;
  }
  // 使用 ch_layout 来获取通道数
  return codecContext->ch_layout.nb_channels;
}

AVSampleFormat AudioDecoder::getSampleFormat() const {
  return codecContext ? codecContext->sample_fmt : AV_SAMPLE_FMT_NONE;
}

// 添加获取音频时长的方法
double AudioDecoder::getDuration() const {
  if (!formatContext || audioStreamIndex < 0) {
    return 0.0;
  }

  AVStream *stream = formatContext->streams[audioStreamIndex];
  if (stream->duration != AV_NOPTS_VALUE) {
    return stream->duration * av_q2d(stream->time_base);
  }

  return formatContext->duration / (double)AV_TIME_BASE;
}