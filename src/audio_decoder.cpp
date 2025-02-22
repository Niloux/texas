#include "audio_decoder.h"

AudioDecoder::AudioDecoder() : isDecoding(false) {
  _logger = Logger::getInstance().getLogger("AudioDecoder");
}

AudioDecoder::~AudioDecoder() {
  if (isDecoding) {
    stop();
  }
  avcodec_free_context(&codecContext);
  avformat_close_input(&formatContext);
}

bool AudioDecoder::open(const std::string &filename) {
  // 打开音频文件
  if (avformat_open_input(&formatContext, filename.c_str(), nullptr, nullptr) !=
      0) {
    _logger->error("Could not open the file!");
    return false;
  }

  // 查找音频流
  if (avformat_find_stream_info(formatContext, nullptr) < 0) {
    _logger->error("Could not find stream information!");
    return false;
  }

  audioStreamIndex = -1;
  for (int i = 0; i < formatContext->nb_streams; i++) {
    if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audioStreamIndex = i;
      break;
    }
  }

  if (audioStreamIndex == -1) {
    _logger->error("Could not find audio stream!");
    return false;
  }

  // 打开解码器
  AVCodecParameters *codecParams =
      formatContext->streams[audioStreamIndex]->codecpar;
  codec = avcodec_find_decoder(codecParams->codec_id);
  if (!codec) {
    _logger->error("Codec not found!");
    return false;
  }

  codecContext = avcodec_alloc_context3(codec);
  if (!codecContext) {
    _logger->error("Could not allocate codec context!");
    return false;
  }

  if (avcodec_parameters_to_context(codecContext, codecParams) < 0) {
    _logger->error("Could not copy codec parameters to context!");
    return false;
  }

  if (avcodec_open2(codecContext, codec, nullptr) < 0) {
    _logger->error("Could not open codec!");
    return false;
  }

  return true;
}

void AudioDecoder::start() {
  isDecoding = true;
  decoderThread = std::thread(&AudioDecoder::decodeLoop, this);
}

void AudioDecoder::stop() {
  isDecoding = false;
  if (decoderThread.joinable()) {
    decoderThread.join();
  }
}

bool AudioDecoder::getAudioFrame(AVFrame **frame) {
  std::unique_lock<std::mutex> lock(frameQueueMutex);
  frameAvailable.wait(lock,
                      [this]() { return !frameQueue.empty() || !isDecoding; });

  if (frameQueue.empty())
    return false;

  *frame = frameQueue.front();
  frameQueue.pop();
  return true;
}

void AudioDecoder::decodeLoop() {
  AVPacket packet;
  AVFrame *frame = av_frame_alloc();

  while (isDecoding) {
    if (av_read_frame(formatContext, &packet) < 0) {
      break; // 如果读取文件结束，退出解码循环
    }

    if (packet.stream_index == audioStreamIndex) {
      if (avcodec_send_packet(codecContext, &packet) == 0) {
        if (avcodec_receive_frame(codecContext, frame) == 0) {
          // 解码成功，存入队列
          std::lock_guard<std::mutex> lock(frameQueueMutex);
          frameQueue.push(av_frame_clone(frame));
          frameAvailable.notify_one();
        }
      }
    }
    av_packet_unref(&packet);
  }

  av_frame_free(&frame);
}