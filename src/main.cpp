#include <gst/gst.h>
#include <gst/video/gstvideodecoder.h>

#include "audio-mixer.hpp"
#include "compositor-layouter/compositor-layouter.hpp"
#include "gstutil/pipeline-helper.hpp"
#include "macros/autoptr.hpp"
#include "macros/unwrap.hpp"
#include "pad-name-parser.hpp"
#include "util/assert.hpp"
#include "util/event.hpp"

namespace {
declare_autoptr(GMainLoop, GMainLoop, g_main_loop_unref);
declare_autoptr(GstMessage, GstMessage, gst_message_unref);
declare_autoptr(GString, gchar, g_free);
declare_autoptr(GstCaps, GstCaps, gst_caps_unref);

auto capsfilter_set_size(GstElement* const capsfilter, const int width, const int height) -> void {
    const auto caps = AutoGstCaps(gst_caps_new_simple("video/x-raw",
                                                      "width", G_TYPE_INT, width,
                                                      "height", G_TYPE_INT, height,
                                                      NULL));
    g_object_set(capsfilter, "caps", caps.get(), NULL);
}

struct Participant {
    std::string id;
    std::string nick;
    uint32_t    audio_ssrc  = 0;
    uint32_t    video_ssrc  = 0;
    bool        audio_muted = true;
    bool        video_muted = true;

    AutoGstObject<GstElement>   video_decoder;
    AutoGstObject<GstElement>   video_converter;
    CompositorLayouter::Source* layouter_source;

    AutoGstObject<GstElement> audio_decoder;
    AutoGstObject<GstElement> audio_converter;
    AudioMixer::Source*       mixer_source;

    auto link_to_compositor(CompositorLayouter& layouter) -> void {
        // link to compositor
        auto videoconvert_src = AutoGstObject(gst_element_get_static_pad(video_converter.get(), "src"));
        assert_n(videoconvert_src);
        const auto layouter_source = layouter.add_src(std::move(videoconvert_src), true);
        // set state
        assert_n(gst_element_sync_state_with_parent(video_decoder.get()) == TRUE);
        assert_n(gst_element_sync_state_with_parent(video_converter.get()) == TRUE);
        this->layouter_source = layouter_source;
    }

    auto unlink_from_compositor(CompositorLayouter& layouter) -> void {
        auto ready = Event();
        layouter.remove_src(layouter_source, [this, &ready](GstPad* /*pad*/) {
            assert_n(gst_element_set_state(video_converter.get(), GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
            assert_n(gst_element_set_state(video_decoder.get(), GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
            ready.wakeup();
        });
        ready.wait();
        this->layouter_source = nullptr;
    }

    auto link_to_mixer(AudioMixer& mixer) -> void {
        // link to compositor
        auto audioconvert_src = AutoGstObject(gst_element_get_static_pad(audio_converter.get(), "src"));
        assert_n(audioconvert_src);
        const auto mixer_source = mixer.add_src(std::move(audioconvert_src));
        // set state
        assert_n(gst_element_sync_state_with_parent(audio_decoder.get()) == TRUE);
        assert_n(gst_element_sync_state_with_parent(audio_converter.get()) == TRUE);
        this->mixer_source = mixer_source;
    }

    auto unlink_from_mixer(AudioMixer& mixer) -> void {
        assert_n(gst_element_set_state(audio_converter.get(), GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
        assert_n(gst_element_set_state(audio_decoder.get(), GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
        this->mixer_source = nullptr;
    }
};

// callbacks
struct Context {
    GstElement* pipeline;

    CompositorLayouter       layouter;
    AudioMixer               mixer;
    std::vector<Participant> participants;

    auto find_participant_by_id(const std::string_view id) -> Participant* {
        for(auto& participant : participants) {
            if(participant.id == id) {
                return &participant;
            }
        }
        return nullptr;
    }
};

auto jitsibin_pad_added_handler(
    GstElement* const jitsibin,
    GstPad* const     pad,
    gpointer const    data) -> void {
    auto& self = *std::bit_cast<Context*>(data);

    const auto name_g = AutoGString(gst_object_get_name(GST_OBJECT(pad)));
    const auto name   = std::string_view(name_g.get());
    PRINT("pad added name=", name);

    unwrap_on(pad_name, parse_jitsibin_pad_name(name));
    unwrap_pn_mut(participant, self.find_participant_by_id(pad_name.participant_id));

    auto audio_decoder_name = (const char*)(nullptr);
    auto video_decoder_name = (const char*)(nullptr);
    if(pad_name.codec == "OPUS") {
        audio_decoder_name = "opusdec";
    } else if(pad_name.codec == "H264") {
        video_decoder_name = "avdec_h264";
    } else if(pad_name.codec == "VP8") {
        video_decoder_name = "avdec_vp8";
    } else if(pad_name.codec == "VP9") {
        video_decoder_name = "avdec_vp9";
    } else {
        WARN("unsupported codec: ", pad_name.codec);
        return;
    }

    const auto current_ssrc = audio_decoder_name != nullptr ? participant.audio_ssrc : participant.video_ssrc;
    if(current_ssrc != 0) {
        WARN("multiple pad added for same codec type, ignoring");
        return;
    }

    if(video_decoder_name != nullptr) {
        // create decoder and converter
        unwrap_pn_mut(decoder, add_new_element_to_pipeine(self.pipeline, video_decoder_name));
        unwrap_pn_mut(videoconvert, add_new_element_to_pipeine(self.pipeline, "videoconvert"));
        // link pad -> decoder
        auto decoder_sink = AutoGstObject(gst_element_get_static_pad(&decoder, "sink"));
        assert_n(decoder_sink.get() != NULL);
        assert_n(gst_pad_link(pad, decoder_sink.get()) == GST_PAD_LINK_OK);
        // link decoder -> converter
        assert_n(gst_element_link_pads(&decoder, NULL, &videoconvert, NULL) == TRUE);

        participant.video_decoder   = AutoGstObject(&decoder);
        participant.video_converter = AutoGstObject(&videoconvert);

        participant.link_to_compositor(self.layouter);
        if(!participant.video_muted) {
            self.layouter.mute_unmute_src(participant.layouter_source, false);
        }

        participant.video_ssrc = pad_name.ssrc;

        PRINT("video connected");
    } else if(audio_decoder_name != nullptr) {
        // create decoder and converter
        unwrap_pn_mut(decoder, add_new_element_to_pipeine(self.pipeline, audio_decoder_name));
        unwrap_pn_mut(audioconvert, add_new_element_to_pipeine(self.pipeline, "audioconvert"));
        // link pad -> decoder
        auto decoder_sink = AutoGstObject(gst_element_get_static_pad(&decoder, "sink"));
        assert_n(decoder_sink.get() != NULL);
        assert_n(gst_pad_link(pad, decoder_sink.get()) == GST_PAD_LINK_OK);
        // link decoder -> converter
        assert_n(gst_element_link_pads(&decoder, NULL, &audioconvert, NULL) == TRUE);

        participant.audio_decoder   = AutoGstObject(&decoder);
        participant.audio_converter = AutoGstObject(&audioconvert);

        participant.link_to_mixer(self.mixer);

        participant.audio_ssrc = pad_name.ssrc;

        PRINT("audio connected");
    }
}

auto jitsibin_pad_removed_handler(
    GstElement* const jitisbin,
    GstPad* const     pad,
    gpointer const    data) -> void {
    const auto name_g = AutoGString(gst_object_get_name(GST_OBJECT(pad)));
    const auto name   = std::string_view(name_g.get());
    // we do not handle pad removal
    // rtpbin only removes pads if the same ssrc is reused by new participant,
    // and this is too rare case.
    WARN("pad removed name=", name);
}

auto jitsibin_participant_joined_handler(
    GstElement* const  jitisbin,
    const gchar* const participant_id,
    const gchar* const nick,
    gpointer const     data) -> void {
    PRINT("participant joined ", participant_id, " ", nick);

    auto& self        = *std::bit_cast<Context*>(data);
    auto  participant = Participant{
         .id   = participant_id,
         .nick = nick,
    };
    self.participants.emplace_back(std::move(participant));
}

auto jitsibin_participant_left_handler(
    GstElement* const  jitisbin,
    const gchar* const participant_id,
    const gchar* const nick,
    gpointer const     data) -> void {
    PRINT("participant left ", participant_id, " ", nick);

    auto& self = *std::bit_cast<Context*>(data);

    auto iter = self.participants.begin();
    for(; iter != self.participants.end(); iter += 1) {
        if(iter->id == participant_id) {
            break;
        }
    }
    assert_n(iter != self.participants.end());
    auto& participant = *iter;

    if(participant.video_ssrc != 0) {
        participant.unlink_from_compositor(self.layouter);
    }
    if(participant.audio_ssrc != 0) {
        participant.unlink_from_mixer(self.mixer);
    }

    self.participants.erase(iter);
}

auto jitsibin_mute_state_changed_handler(
    GstElement* const  jitisbin,
    const gchar* const participant_id,
    const gboolean     is_audio,
    const gboolean     new_muted,
    gpointer const     data) -> void {
    PRINT("mute state changed ", participant_id, " ", is_audio ? "audio" : "video", "=", new_muted);

    auto& self = *std::bit_cast<Context*>(data);
    unwrap_pn_mut(participant, self.find_participant_by_id(participant_id));
    if(is_audio) {
        participant.audio_muted = new_muted;
        // we do nothing on audio (un)mute.
    } else {
        participant.video_muted = new_muted;
        if(participant.video_ssrc != 0) {
            self.layouter.mute_unmute_src(participant.layouter_source, new_muted);
        }
    }
}

struct Args {
    std::string source_server;
    std::string source_room;
    std::string source_nick;
    std::string sink_server;
    std::string sink_room;
    std::string sink_nick;
    int         output_width;
    int         output_height;
    bool        insecure;
};

auto run(const Args& args) -> bool {
    const auto pipeline = AutoGstObject(gst_pipeline_new(NULL));
    assert_b(pipeline.get() != NULL);

    // jitsibin -> (decoder) -> videoconvert -> compositor -> videoscale -> capsfilter -> x264enc -> jitsibin
    //          -> (decoder) -> audioconvert -> audiomixer ->                             opusenc ->

    // create jitsibin
    unwrap_pb_mut(jitsibin_src, add_new_element_to_pipeine(pipeline.get(), "jitsibin"));
    g_object_set(&jitsibin_src,
                 "server", args.source_server.data(),
                 "room", args.source_room.data(),
                 "nick", args.source_nick.data(),
                 "insecure", args.insecure ? TRUE : FALSE,
                 "receive-limit", -1,
                 "verbose", TRUE,
                 "dump-websocket-packets", TRUE,
                 "lws-loglevel-bitmap", 0b11,
                 NULL);
    unwrap_pb_mut(jitsibin_sink, add_new_element_to_pipeine(pipeline.get(), "jitsibin"));
    g_object_set(&jitsibin_sink,
                 "server", args.sink_server.data(),
                 "room", args.sink_room.data(),
                 "nick", args.sink_nick.data(),
                 "insecure", args.insecure ? TRUE : FALSE,
                 "force-play", TRUE,
                 "verbose", TRUE,
                 "dump-websocket-packets", TRUE,
                 "lws-loglevel-bitmap", 0b11,
                 NULL);

    // video elements
    unwrap_pb_mut(compositor, add_new_element_to_pipeine(pipeline.get(), "compositor"));
    unwrap_pb_mut(videoscale, add_new_element_to_pipeine(pipeline.get(), "videoscale"));
    unwrap_pb_mut(capsfilter, add_new_element_to_pipeine(pipeline.get(), "capsfilter"));
    capsfilter_set_size(&capsfilter, args.output_width, args.output_height);
    unwrap_pb_mut(videoenc, add_new_element_to_pipeine(pipeline.get(), "x264enc"));

    assert_b(gst_element_link_pads(&compositor, NULL, &videoscale, NULL) == TRUE);
    assert_b(gst_element_link_pads(&videoscale, NULL, &capsfilter, NULL) == TRUE);
    assert_b(gst_element_link_pads(&capsfilter, NULL, &videoenc, NULL) == TRUE);
    assert_b(gst_element_link_pads(&videoenc, NULL, &jitsibin_sink, "video_sink") == TRUE);

    // audio elements
    unwrap_pb_mut(audiomixer, add_new_element_to_pipeine(pipeline.get(), "audiomixer"));
    unwrap_pb_mut(audioenc, add_new_element_to_pipeine(pipeline.get(), "opusenc"));

    assert_b(gst_element_link_pads(&audiomixer, NULL, &audioenc, NULL) == TRUE);
    assert_b(gst_element_link_pads(&audioenc, NULL, &jitsibin_sink, "audio_sink") == TRUE);

    auto context = Context{
        .pipeline = pipeline.get(),
        .layouter = CompositorLayouter(&compositor),
        .mixer    = AudioMixer(&audiomixer),
    };
    context.layouter.verbose = true;

    g_signal_connect(&jitsibin_src, "pad-added", G_CALLBACK(jitsibin_pad_added_handler), &context);
    g_signal_connect(&jitsibin_src, "pad-removed", G_CALLBACK(jitsibin_pad_removed_handler), &context);
    g_signal_connect(&jitsibin_src, "participant-joined", G_CALLBACK(jitsibin_participant_joined_handler), &context);
    g_signal_connect(&jitsibin_src, "participant-left", G_CALLBACK(jitsibin_participant_left_handler), &context);
    g_signal_connect(&jitsibin_src, "mute-state-changed", G_CALLBACK(jitsibin_mute_state_changed_handler), &context);

    return run_pipeline(pipeline.get());
}
} // namespace

auto main(int argc, char* argv[]) -> int {
    gst_init(&argc, &argv);

    const auto args = Args{
        .source_server = "jitsi.local",
        .source_room   = "src",
        .source_nick   = "agent-src",
        .sink_server   = "jitsi.local",
        .sink_room     = "sink",
        .sink_nick     = "agent-sink",
        .output_width  = 640,
        .output_height = 360,
        .insecure      = true,
    };
    return run(args) ? 1 : 0;
}
