#include <iostream>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <unistd.h>
#include <memory>
#include <mutex>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

std::string changeExtensionToMp4(const std::string& input) {
    std::string output = input;
    size_t pos = output.find_last_of('.');
    if (pos != std::string::npos) {
        output.replace(pos, std::string::npos, ".mp4");
    } else {
        output += ".mp4";
    }
    return output;
}

bool fileExists(const std::string& filename) {
    return std::fopen(filename.c_str(), "r") != nullptr;
}

std::string getAbsolutePath(const std::string& filename) {
    char buffer[1024];
    if (getcwd(buffer, sizeof(buffer)) != nullptr) {
        return std::string(buffer) + "/" + filename;
    } else {
        throw std::runtime_error("Could not get current working directory.");
    }
}

std::shared_ptr<AVFrame> allocate_frame(AVPixelFormat pix_fmt, int width, int height) {
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        throw std::runtime_error("Could not allocate frame.");
    }
    frame->format = pix_fmt;
    frame->width = width;
    frame->height = height;
    if (av_frame_get_buffer(frame, 32) < 0) {
        av_frame_free(&frame);
        throw std::runtime_error("Could not allocate the video frame data.");
    }
    return std::shared_ptr<AVFrame>(frame, [](AVFrame* f) { av_frame_free(&f); });
}

void encode_frame(std::shared_ptr<AVFrame> frame, AVCodecContext* output_codec_context, std::shared_ptr<AVFrame> output_frame, SwsContext* sws_ctx, AVStream* output_video_stream, AVFormatContext* output_format_context, std::mutex& mutex, int64_t& frame_counter) {
    AVPacket* output_packet = av_packet_alloc();
    if (!output_packet) {
        throw std::runtime_error("Could not allocate AVPacket.");
    }

    sws_scale(
        sws_ctx, frame->data, frame->linesize, 0, frame->height,
        output_frame->data, output_frame->linesize
    );

    output_frame->pts = frame_counter++;

    int response = avcodec_send_frame(output_codec_context, output_frame.get());
    if (response < 0) {
        av_packet_free(&output_packet);
        throw std::runtime_error("Error while sending frame to encoder: " + std::to_string(response));
    }

    while (response >= 0) {
        response = avcodec_receive_packet(output_codec_context, output_packet);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        } else if (response < 0) {
            av_packet_free(&output_packet);
            throw std::runtime_error("Error while receiving packet from encoder: " + std::to_string(response));
        }

        av_packet_rescale_ts(output_packet, output_codec_context->time_base, output_video_stream->time_base);
        output_packet->stream_index = output_video_stream->index;

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (av_interleaved_write_frame(output_format_context, output_packet) < 0) {
                av_packet_free(&output_packet);
                throw std::runtime_error("Error while writing packet to output file.");
            }
        }
        av_packet_unref(output_packet);
    }

    av_packet_free(&output_packet);
}

void encode_pass(const char* input_filename, const char* output_filename, bool is_first_pass) {
    AVFormatContext* input_format_context = nullptr;
    if (avformat_open_input(&input_format_context, input_filename, nullptr, nullptr) != 0) {
        throw std::runtime_error("Could not open input file.");
    }

    if (avformat_find_stream_info(input_format_context, nullptr) < 0) {
        avformat_close_input(&input_format_context);
        throw std::runtime_error("Could not find stream info.");
    }

    int video_stream_index = av_find_best_stream(input_format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_index < 0) {
        avformat_close_input(&input_format_context);
        throw std::runtime_error("Could not find video stream in the input file.");
    }

    AVStream* input_video_stream = input_format_context->streams[video_stream_index];
    const AVCodec* input_codec = avcodec_find_decoder(input_video_stream->codecpar->codec_id);
    AVCodecContext* input_codec_context = avcodec_alloc_context3(input_codec);
    avcodec_parameters_to_context(input_codec_context, input_video_stream->codecpar);
    if (avcodec_open2(input_codec_context, input_codec, nullptr) < 0) {
        avcodec_free_context(&input_codec_context);
        avformat_close_input(&input_format_context);
        throw std::runtime_error("Could not open input codec.");
    }

    AVFormatContext* output_format_context = nullptr;
    avformat_alloc_output_context2(&output_format_context, nullptr, "mp4", output_filename);
    if (!output_format_context) {
        avcodec_free_context(&input_codec_context);
        throw std::runtime_error("Could not create output context.");
    }

    const AVCodec* output_codec = avcodec_find_encoder_by_name("libx264");
    if (!output_codec) {
        avcodec_free_context(&input_codec_context);
        avformat_free_context(output_format_context);
        throw std::runtime_error("Could not find libx264 codec.");
    }

    AVStream* output_video_stream = avformat_new_stream(output_format_context, nullptr);
    if (!output_video_stream) {
        avcodec_free_context(&input_codec_context);
        avformat_free_context(output_format_context);
        throw std::runtime_error("Could not create output stream.");
    }

    AVCodecContext* output_codec_context = avcodec_alloc_context3(output_codec);
    output_codec_context->height = input_codec_context->height;
    output_codec_context->width = input_codec_context->width;
    output_codec_context->sample_aspect_ratio = input_codec_context->sample_aspect_ratio;
    output_codec_context->pix_fmt = AV_PIX_FMT_YUV420P;
    output_codec_context->time_base = (AVRational){1, 60}; 

    if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER) {
        output_codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }


    output_codec_context->thread_count = 32;
    output_codec_context->thread_type = FF_THREAD_FRAME;

  
    output_codec_context->bit_rate = 500000; 
    output_codec_context->gop_size = 60; 
    output_codec_context->max_b_frames = 3; 

    AVDictionary* codec_options = nullptr;
    av_dict_set(&codec_options, "preset", "veryfast", 0); 
    av_dict_set(&codec_options, "crf", "28", 0); 
    av_dict_set(&codec_options, "tune", "film", 0); 
    av_dict_set(&codec_options, "profile", "high", 0); 

    if (is_first_pass) {
        av_dict_set(&codec_options, "pass", "1", 0); 
        av_dict_set(&codec_options, "b:v", "500k", 0); 
    } else {
        av_dict_set(&codec_options, "pass", "2", 0); 
        av_dict_set(&codec_options, "b:v", "500k", 0); 
    }

    if (avcodec_open2(output_codec_context, output_codec, &codec_options) < 0) {
        av_dict_free(&codec_options);
        avcodec_free_context(&input_codec_context);
        avcodec_free_context(&output_codec_context);
        avformat_free_context(output_format_context);
        throw std::runtime_error("Could not open output codec.");
    }
    av_dict_free(&codec_options);

    avcodec_parameters_from_context(output_video_stream->codecpar, output_codec_context);
    output_video_stream->time_base = output_codec_context->time_base;
    output_video_stream->avg_frame_rate = (AVRational){60, 1}; 

    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output_format_context->pb, output_filename, AVIO_FLAG_WRITE) < 0) {
            avcodec_free_context(&input_codec_context);
            avcodec_free_context(&output_codec_context);
            avformat_free_context(output_format_context);
            throw std::runtime_error("Could not open output file.");
        }
    }

    if (avformat_write_header(output_format_context, nullptr) < 0) {
        avcodec_free_context(&input_codec_context);
        avcodec_free_context(&output_codec_context);
        avformat_free_context(output_format_context);
        throw std::runtime_error("Could not write output file header.");
    }

    AVPacket packet;
    auto frame = allocate_frame(input_codec_context->pix_fmt, input_codec_context->width, input_codec_context->height);
    auto output_frame = allocate_frame(output_codec_context->pix_fmt, output_codec_context->width, output_codec_context->height);

    SwsContext* sws_ctx = sws_getContext(
        input_codec_context->width, input_codec_context->height, input_codec_context->pix_fmt,
        output_codec_context->width, output_codec_context->height, output_codec_context->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    std::mutex mutex;
    int64_t frame_counter = 0;

    while (av_read_frame(input_format_context, &packet) >= 0) {
        if (packet.stream_index == video_stream_index) {
            int response = avcodec_send_packet(input_codec_context, &packet);
            if (response < 0) {
                av_packet_unref(&packet);
                throw std::runtime_error("Error while sending packet to decoder.");
            }

            while (response >= 0) {
                response = avcodec_receive_frame(input_codec_context, frame.get());
                if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                    break;
                } else if (response < 0) {
                    av_packet_unref(&packet);
                    throw std::runtime_error("Error while receiving frame from decoder.");
                }

                std::shared_ptr<AVFrame> frame_copy(av_frame_clone(frame.get()), [](AVFrame* f) { av_frame_free(&f); });
                if (!frame_copy) {
                    av_packet_unref(&packet);
                    throw std::runtime_error("Could not clone frame.");
                }

                encode_frame(frame_copy, output_codec_context, output_frame, sws_ctx, output_video_stream, output_format_context, mutex, frame_counter);
            }
        }
        av_packet_unref(&packet);
    }

    int response = avcodec_send_frame(output_codec_context, nullptr);
    if (response < 0) {
        throw std::runtime_error("Error while sending flush frame to encoder.");
    }

    while (response >= 0) {
        AVPacket* output_packet = av_packet_alloc();
        if (!output_packet) {
            throw std::runtime_error("Could not allocate AVPacket.");
        }

        response = avcodec_receive_packet(output_codec_context, output_packet);
        if (response == AVERROR_EOF) {
            av_packet_free(&output_packet);
            break;
        } else if (response < 0) {
            av_packet_free(&output_packet);
            throw std::runtime_error("Error while receiving packet from encoder.");
        }

        av_packet_rescale_ts(output_packet, output_codec_context->time_base, output_video_stream->time_base);
        output_packet->stream_index = output_video_stream->index;

        if (av_interleaved_write_frame(output_format_context, output_packet) < 0) {
            av_packet_free(&output_packet);
            throw std::runtime_error("Error while writing packet to output file.");
        }

        av_packet_free(&output_packet);
    }

    av_write_trailer(output_format_context);

    avcodec_free_context(&input_codec_context);
    avcodec_free_context(&output_codec_context);
    avformat_close_input(&input_format_context);
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_format_context->pb);
    }
    avformat_free_context(output_format_context);
    sws_freeContext(sws_ctx);
}

void encode(const char* input_filename, const char* output_filename) {
    encode_pass(input_filename, "/dev/null", true);
    encode_pass(input_filename, output_filename, false);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input file>\n";
        return 1;
    }

    std::string input_filename = argv[1];

    if (!fileExists(input_filename)) {
        std::cerr << "Input file does not exist: " << input_filename << "\n";
        return 1;
    }

    std::string output_filename = changeExtensionToMp4(input_filename);

    try {
        encode(input_filename.c_str(), output_filename.c_str());
        std::cout << "Encoding completed successfully.\n";
        std::cout << "Output file saved to: " << getAbsolutePath(output_filename) << "\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}
