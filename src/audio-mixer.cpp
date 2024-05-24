#include "audio-mixer.hpp"
#include "macros/unwrap.hpp"
#include "util/assert.hpp"

auto AudioMixer::add_src(AutoGstObject<GstPad> upstream_pad) -> Source* {
    // generate new pad name
    auto sink_name = std::array<char, 16>();
    snprintf(sink_name.data(), sink_name.size(), "sink_%u", sink_id_serial.fetch_add(1));

    // request new pad from compositor
    unwrap_pp_mut(factory, gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(mixer), "sink_%u"));
    auto mixer_pad = AutoGstObject(gst_element_request_pad(mixer, &factory, sink_name.data(), NULL));
    assert_p(mixer_pad.get() != NULL);

    // link src to compositor
    assert_p(gst_pad_link(upstream_pad.get(), mixer_pad.get()) == GST_PAD_LINK_OK);

    const auto source_ptr = new Source{
        .upstream_pad = std::move(upstream_pad),
        .mixer_pad    = std::move(mixer_pad),
    };
    sources.emplace_back(source_ptr);
    return source_ptr;
}

auto AudioMixer::remove_src(const Source* const source_ptr, const std::function<void(GstPad*)> pad_delete_callback) -> void {
    auto source = std::unique_ptr<Source>();
    for(auto i = sources.begin(); i != sources.end(); i += 1) {
        if(i->get() == source_ptr) {
            source = std::move(*i);
            sources.erase(i);
            break;
        }
    }
    assert_n(source);
    DYN_ASSERT(gst_pad_unlink(source->upstream_pad.get(), source->mixer_pad.get()) == TRUE);
    gst_element_release_request_pad(mixer, source->mixer_pad.get());
}

AudioMixer::AudioMixer(GstElement* const mixer) : mixer(mixer) {}
