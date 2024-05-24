#pragma once
#include <vector>

#include <gst/gst.h>

#include "compositor-layouter/gutil/auto-gst-object.hpp"

struct AudioMixer {
    struct Source {
        AutoGstObject<GstPad> upstream_pad;
        AutoGstObject<GstPad> mixer_pad;
    };

    GstElement*      mixer;
    std::atomic_uint sink_id_serial = 0;

    std::vector<std::unique_ptr<Source>> sources;

    auto add_src(AutoGstObject<GstPad> upstream_pad) -> Source*;
    auto remove_src(const Source* const source_ptr) -> void;

    AudioMixer(GstElement* mixer);
};
