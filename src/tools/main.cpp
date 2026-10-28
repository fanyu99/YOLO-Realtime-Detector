#include <iostream>
#include <string>
#include <filesystem>
extern "C" {
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}



namespace {
std::string ffmpegError(int errorCode) {
    char errorBuffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(errorCode, errorBuffer, sizeof(errorBuffer));
    return errorBuffer;
}

void printError(const char* operation, int errorCode) {
    std::cerr << operation << " failed: " << ffmpegError(errorCode)
              << " (" << errorCode << ")\n";
}

void printFrameInfo(const AVFrame* frame, int frameIndex, AVRational timeBase) {
    const char* pixelFormatName = av_get_pix_fmt_name(
        static_cast<AVPixelFormat>(frame->format));
    const int64_t timestamp = frame->best_effort_timestamp;
    const double timestampSeconds =
        timestamp == AV_NOPTS_VALUE ? -1.0 : timestamp * av_q2d(timeBase);

    std::cout << "frame=" << frameIndex
              << ", width=" << frame->width
              << ", height=" << frame->height
              << ", pixel_format="
              << (pixelFormatName != nullptr ? pixelFormatName : "unknown")
              << ", pts="
              << (timestamp == AV_NOPTS_VALUE
                      ? std::string("NOPTS")
                      : std::to_string(timestamp))
              << ", pts_seconds=" << timestampSeconds << '\n';
}
} // namespace

int main(int argc, char* argv[]) {
    // 也可以通过命令行传入路径：ffmpeg_learning.exe D:\\path\\video.mp4
    const std::filesystem::path defaultInputPath =
        L"D:\\测试视频\\12142816_1920_1080_30fps.mp4";
    const std::filesystem::path inputPath =
        argc >= 2 ? std::filesystem::path(argv[1]) : defaultInputPath;

    std::cout << "inputPath: " << inputPath.string() << '\n';
    const std::string inputFileName = inputPath.string();
    AVFormatContext* formatContext = nullptr;
    // 打开文件
    int ret = avformat_open_input(&formatContext, inputFileName.c_str(), nullptr, nullptr);
    if (ret < 0) {
        printError("avformat_open_input", ret);
        return 1;
    }

    // 获取流信息
    ret = avformat_find_stream_info(formatContext, nullptr);
    if (ret < 0) {
        printError("avformat_find_stream_info", ret);
        avformat_close_input(&formatContext);
        return 1;
    }

    // 获取视频流索引
    const int videoStreamIndex = av_find_best_stream(
        formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex < 0) {
        printError("av_find_best_stream", videoStreamIndex);
        avformat_close_input(&formatContext);
        return 1;
    }
    // 获取视频流和解码器
    AVStream* videoStream = formatContext->streams[videoStreamIndex];
    const AVCodec* decoder =
        avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (decoder == nullptr) {
        std::cerr << "No decoder found for codec id: "
                  << videoStream->codecpar->codec_id << '\n';
        avformat_close_input(&formatContext);
        return 1;
    }

    std::cout << "video stream index: " << videoStreamIndex << '\n'
              << "decoder name: " << decoder->name << '\n'
              << "resolution: " << videoStream->codecpar->width
              << 'x' << videoStream->codecpar->height << '\n'
              << "time base: " << videoStream->time_base.num
              << '/' << videoStream->time_base.den << "\n\n";
	// 分配解码器上下文
    AVCodecContext* codecContext = avcodec_alloc_context3(decoder);
    if (codecContext == nullptr) {
        std::cerr << "avcodec_alloc_context3 failed\n";
        avformat_close_input(&formatContext);
        return 1;
    }
	// 将视频流的参数复制到解码器上下文
    ret = avcodec_parameters_to_context(codecContext, videoStream->codecpar);
    if (ret < 0) {
        printError("avcodec_parameters_to_context", ret);
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        return 1;
    }
    	// 打开解码器
    ret = avcodec_open2(codecContext, decoder, nullptr);
    if (ret < 0) {
        printError("avcodec_open2", ret);
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        return 1;
    }
    // 分配压缩包和帧内存
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (packet == nullptr || frame == nullptr) {
        std::cerr << "Failed to allocate AVPacket or AVFrame\n";
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        return 1;
    }

    // 读取压缩包并解码
    int frameIndex = 0;      
    while ((ret = av_read_frame(formatContext, packet)) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
			// 发送压缩包到解码器进行解码
            ret = avcodec_send_packet(codecContext, packet);
            if (ret < 0) {
                printError("avcodec_send_packet", ret);
                av_packet_unref(packet);
                break;
            }
            // 接收解码后的帧
            // 一个 packet 可能产生 0、1 或多帧，所以必须循环接收。
            while (true) {
                ret = avcodec_receive_frame(codecContext, frame);
                if (ret == 0) {
                    printFrameInfo(frame, frameIndex++, videoStream->time_base);
                    continue;
                }
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }

                printError("avcodec_receive_frame", ret);
                av_packet_unref(packet);
                av_packet_free(&packet);
                av_frame_free(&frame);
                avcodec_free_context(&codecContext);
                avformat_close_input(&formatContext);
                return 1;
            }
        }
        av_packet_unref(packet);
    }

    if (ret < 0 && ret != AVERROR_EOF) {
        printError("av_read_frame", ret);
    }

    // 文件读取结束后发送空 packet，冲刷解码器内部缓存的剩余帧。
    ret = avcodec_send_packet(codecContext, nullptr);
    if (ret < 0 && ret != AVERROR_EOF) {
        printError("avcodec_send_packet (flush)", ret);
    } else {
        // 接收缓冲区内剩余的帧
        while (true) {
            ret = avcodec_receive_frame(codecContext, frame);
            if (ret == 0) {
                printFrameInfo(frame, frameIndex++, videoStream->time_base);
                continue;
            }
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            printError("avcodec_receive_frame (flush)", ret);
            break;
        }
    }

    std::cout << "decoded frames: " << frameIndex << '\n';
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    return 0;
}