/******************************************************************************
    Copyright (C) 2026 ekeer <ekeer@github.com>
    This file is part of obs-vst3.
    It uses the Steinberg VST3 SDK, which is licensed under MIT license.
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "VST3Graph.h"
#include "VST3Plugin.h"
#include "obs-vst3.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <obs-module.h>
#include <util/platform.h>

/* =============== VST3GraphNode =============== */

VST3GraphNode::VST3GraphNode(size_t id,
                               std::shared_ptr<VST3Plugin> plugin)
    : id_(id)
    , plugin_(std::move(plugin))
    , name_(plugin_ ? plugin_->name : "empty")
{
    if (plugin_) {
        channels_ = plugin_->numEnabledOutputAudioBuses > 0
                        ? plugin_->mainOutputBusNumChannels
                        : 2;
        if (channels_ < 1)
            channels_ = 2;
    }
}

VST3GraphNode::~VST3GraphNode()
{
    for (size_t i = 0; i < channels_; i++) {
        if (i < input_buffers_.size())
            deque_free(&input_buffers_[i]);
        if (i < output_buffers_.size())
            deque_free(&output_buffers_[i]);
    }
    if (copy_buffers_) {
        bfree(copy_buffers_[0]);
        bfree(copy_buffers_);
    }
}

VST3GraphNode::VST3GraphNode(VST3GraphNode &&other) noexcept
    : id_(other.id_)
    , plugin_(std::move(other.plugin_))
    , enabled_(other.enabled_)
    , name_(std::move(other.name_))
    , input_buffers_(std::move(other.input_buffers_))
    , output_buffers_(std::move(other.output_buffers_))
    , copy_buffers_(other.copy_buffers_)
    , channels_(other.channels_)
    , frame_size_(other.frame_size_)
    , sample_rate_(other.sample_rate_)
{
    other.copy_buffers_ = nullptr;
    other.channels_ = 0;
}

VST3GraphNode &VST3GraphNode::operator=(VST3GraphNode &&other) noexcept
{
    if (this != &other) {
        id_ = other.id_;
        plugin_ = std::move(other.plugin_);
        enabled_ = other.enabled_;
        name_ = std::move(other.name_);
        input_buffers_ = std::move(other.input_buffers_);
        output_buffers_ = std::move(other.output_buffers_);
        copy_buffers_ = other.copy_buffers_;
        channels_ = other.channels_;
        frame_size_ = other.frame_size_;
        sample_rate_ = other.sample_rate_;
        other.copy_buffers_ = nullptr;
        other.channels_ = 0;
    }
    return *this;
}

bool VST3GraphNode::process(uint32_t frames)
{
    if (!enabled_ || !plugin_) {
        return false;
    }

    size_t segment_size = frames * sizeof(float);

    // Ensure we have enough input data
    for (size_t i = 0; i < channels_; i++) {
        if (input_buffers_[i].size < segment_size) {
            deque_push_back_zero(&input_buffers_[i], segment_size);
        }
    }

    // Pop from input deques into copy buffers
    for (size_t i = 0; i < channels_; i++) {
        deque_pop_front(&input_buffers_[i],
                        copy_buffers_[i],
                        frames * sizeof(float));
    }

    // Copy to VST3 input buses
    for (size_t ch = 0; ch < channels_; ++ch) {
        float *inBuf = copy_buffers_[ch];
        float *vstIn = plugin_->channelBuffer32(
            Steinberg::Vst::kInput, (int32_t)ch);
        if (inBuf && vstIn) {
            memcpy(vstIn, inBuf, frames * sizeof(float));
        }
    }

    // Process through VST3
    plugin_->process(frames);

    // Retrieve from VST3 output buses
    for (size_t ch = 0; ch < channels_; ++ch) {
        uint8_t *outBuf = (uint8_t *)copy_buffers_[ch];
        float *vstOut = plugin_->channelBuffer32(
            Steinberg::Vst::kOutput, (int32_t)ch);
        if (outBuf && vstOut) {
            memcpy(outBuf, vstOut, frames * sizeof(float));
        }
    }

    // Push to output deques
    for (size_t i = 0; i < channels_; i++) {
        deque_push_back(&output_buffers_[i],
                        copy_buffers_[i],
                        frames * sizeof(float));
    }

    return true;
}

void VST3GraphNode::reset()
{
    for (size_t i = 0; i < channels_; i++) {
        if (i < input_buffers_.size())
            deque_pop_front(&input_buffers_[i], nullptr,
                            input_buffers_[i].size);
        if (i < output_buffers_.size())
            deque_pop_front(&output_buffers_[i], nullptr,
                            output_buffers_[i].size);
    }
}

size_t VST3GraphNode::bufferSize() const
{
    if (output_buffers_.empty())
        return 0;
    return output_buffers_[0].size;
}

/* =============== VST3Graph =============== */

VST3Graph::VST3Graph() = default;
VST3Graph::~VST3Graph() = default;

size_t VST3Graph::addNode(std::shared_ptr<VST3Plugin> plugin)
{
    size_t id = next_id_++;
    VST3GraphNode node(id, std::move(plugin));
    node.frame_size_ = frame_size_;
    node.sample_rate_ = sample_rate_;

    // Allocate buffers for this node
    size_t channels = node.channels_;
    node.copy_buffers_ = (float **)bmalloc(
        sizeof(float *) * channels);
    node.copy_buffers_[0] = (float *)bmalloc(
        (size_t)frame_size_ * channels * sizeof(float));

    for (size_t c = 1; c < channels; ++c) {
        node.copy_buffers_[c] =
            node.copy_buffers_[c - 1] + frame_size_;
    }

    node.input_buffers_.resize(channels);
    node.output_buffers_.resize(channels);
    for (size_t i = 0; i < channels; i++) {
        deque_reserve(&node.input_buffers_[i],
                      8 * frame_size_ * sizeof(float));
        deque_reserve(&node.output_buffers_[i],
                      8 * frame_size_ * sizeof(float));
    }

    nodes_.push_back(std::move(node));
    return id;
}

bool VST3Graph::removeNode(size_t node_id)
{
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
        [node_id](const VST3GraphNode &n) {
            return n.id_ == node_id;
        });
    if (it == nodes_.end())
        return false;
    nodes_.erase(it);
    return true;
}

bool VST3Graph::moveNode(size_t node_id, size_t new_index)
{
    if (new_index >= nodes_.size())
        return false;

    auto it = std::find_if(nodes_.begin(), nodes_.end(),
        [node_id](const VST3GraphNode &n) {
            return n.id_ == node_id;
        });
    if (it == nodes_.end())
        return false;

    VST3GraphNode node = std::move(*it);
    nodes_.erase(it);
    nodes_.insert(nodes_.begin() + new_index, std::move(node));
    return true;
}

void VST3Graph::clear()
{
    nodes_.clear();
    next_id_ = 1;
}

VST3GraphNode *VST3Graph::getNode(size_t node_id)
{
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
        [node_id](const VST3GraphNode &n) {
            return n.id_ == node_id;
        });
    return it != nodes_.end() ? &(*it) : nullptr;
}

const VST3GraphNode *VST3Graph::getNode(size_t node_id) const
{
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
        [node_id](const VST3GraphNode &n) {
            return n.id_ == node_id;
        });
    return it != nodes_.end() ? &(*it) : nullptr;
}

void VST3Graph::ensureTempBuffer(size_t needed)
{
    if (temp_buffer_size_ < needed) {
        bfree(temp_buffer_);
        temp_buffer_ = (float *)bmalloc(needed);
        temp_buffer_size_ = needed;
    }
}

bool VST3Graph::processChain(const float *const *input_data,
                              float **output_data,
                              uint32_t frames,
                              uint32_t channels,
                              uint32_t sample_rate)
{
    if (nodes_.empty()) {
        // Pass-through: copy input to output
        for (size_t c = 0; c < channels; c++) {
            memcpy(output_data[c], input_data[c],
                   frames * sizeof(float));
        }
        return false;
    }

    sample_rate_ = sample_rate;
    channels_ = channels;
    frame_size_ = frames;

    size_t segment_size = frames * sizeof(float);

    // --- First node: feed from input_data ---
    VST3GraphNode &first = nodes_.front();
    if (first.enabled_ && first.plugin_) {
        for (size_t c = 0; c < channels; c++) {
            deque_push_back(&first.input_buffers_[c],
                            input_data[c], segment_size);
        }
    }

    // --- Process all nodes in order (serial chain) ---
    for (size_t n = 0; n < nodes_.size(); n++) {
        VST3GraphNode &node = nodes_[n];
        if (!node.enabled_ || !node.plugin_) {
            continue;
        }

        node.channels_ = channels;
        node.frame_size_ = frames;
        node.sample_rate_ = sample_rate;

        node.process(frames);

        // Feed output of this node into the next node's input
        if (n + 1 < nodes_.size()) {
            VST3GraphNode &next = nodes_[n + 1];
            if (next.enabled_ && next.plugin_) {
                for (size_t c = 0; c < channels; c++) {
                    size_t out_size =
                        node.output_buffers_[c].size;
                    if (out_size >= segment_size) {
                        ensureTempBuffer(segment_size);
                        deque_pop_front(
                            &node.output_buffers_[c],
                            temp_buffer_, segment_size);
                        deque_push_back(
                            &next.input_buffers_[c],
                            temp_buffer_, segment_size);
                    } else {
                        // No output yet, push silence
                        deque_push_back_zero(
                            &next.input_buffers_[c],
                            segment_size);
                    }
                }
            }
        }
    }

    // --- Last node: collect output ---
    VST3GraphNode &last = nodes_.back();
    if (last.enabled_ && last.plugin_) {
        // Wait for enough output from the last node
        if (last.output_buffers_[0].size < segment_size) {
            return false;
        }

        for (size_t c = 0; c < channels; c++) {
            deque_pop_front(&last.output_buffers_[c],
                            output_data[c], segment_size);
        }
    } else {
        // Last node is disabled, pass through
        for (size_t c = 0; c < channels; c++) {
            memset(output_data[c], 0, frames * sizeof(float));
        }
    }

    return true;
}

/* ---------- Serialization ---------- */

std::string VST3Graph::toJsonString()
{
    std::ostringstream json;
    json << "{\n";
    json << "  \"version\": 1,\n";
    json << "  \"nodes\": [\n";

    for (size_t i = 0; i < nodes_.size(); i++) {
        if (i > 0) json << ",\n";
        json << "    {\n";
        json << "      \"id\": " << nodes_[i].id_ << ",\n";
        json << "      \"name\": \""
             << nodes_[i].name_ << "\",\n";
        json << "      \"enabled\": "
             << (nodes_[i].enabled_ ? "true" : "false") << ",\n";
        json << "      \"vst3_id\": \""
             << (nodes_[i].plugin_
                     ? nodes_[i].plugin_->name
                     : "")
             << "\",\n";
        json << "      \"vst3_path\": \""
             << (nodes_[i].plugin_
                     ? nodes_[i].plugin_->path
                     : "")
             << "\"\n";
        json << "    }";
    }

    json << "\n  ]\n";
    json << "}\n";
    return json.str();
}

bool VST3Graph::fromJsonString(const std::string &json)
{
    UNUSED_PARAMETER(json);
    // Minimal JSON parser for graph config
    // In production this should use a proper JSON library
    // For now we clear and return false to let the caller handle it
    clear();
    blog(LOG_INFO, "[VST3Graph] JSON deserialization stub called");
    // TODO: implement proper JSON parsing
    return true;
}

bool VST3Graph::saveToJson(const std::string &path)
{
    std::ofstream file(path);
    if (!file.is_open()) {
        blog(LOG_WARNING, "[VST3Graph] Failed to save to %s",
             path.c_str());
        return false;
    }
    file << toJsonString();
    return true;
}

bool VST3Graph::loadFromJson(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        blog(LOG_WARNING, "[VST3Graph] Failed to load from %s",
             path.c_str());
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return fromJsonString(buffer.str());
}

uint32_t VST3Graph::getTotalLatency() const
{
    uint32_t total = 0;
    for (const auto &node : nodes_) {
        if (node.enabled_ && node.plugin_) {
            total += 0; // FIXME: get actual latency
        }
    }
    return total;
}

/* =============== VST3GraphFilter =============== */

VST3GraphFilter::VST3GraphFilter()
    : graph_(std::make_unique<VST3Graph>())
{
}

VST3GraphFilter::~VST3GraphFilter()
{
    for (size_t i = 0; i < 8; i++) {
        deque_free(&input_buffers_[i]);
        deque_free(&output_buffers_[i]);
    }
    bfree(copy_buffers_[0]);
    deque_free(&info_buffer_);
    da_free(output_data_);
}

/* =============== Graph Filter OBS Source =============== */

static const char *vst3_graph_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("VST3.Graph");
}

void *vst3_graph_create(obs_data_t *settings, obs_source_t *filter)
{
    auto *gf = new VST3GraphFilter();
    gf->context_ = filter;

    audio_t *audio = obs_get_audio();
    const struct audio_output_info *aoi =
        audio_output_get_info(audio);

    gf->frame_size_ = FRAME_SIZE;
    gf->channels_ = audio_output_get_channels(audio);
    gf->sample_rate_ = audio_output_get_sample_rate(audio);
    gf->layout_ = aoi->speakers;
    gf->graph_->setSampleRate((uint32_t)gf->sample_rate_);
    gf->graph_->setChannels((uint32_t)gf->channels_);
    gf->graph_->setFrameSize(gf->frame_size_);

    gf->latency_ = 1000000000LL / (1000 / BUFFER_SIZE_MSEC);

    // Allocate copy buffers
    gf->copy_buffers_[0] = (float *)bmalloc(
        FRAME_SIZE * gf->channels_ * sizeof(float));
    for (size_t c = 1; c < gf->channels_; ++c) {
        gf->copy_buffers_[c] =
            gf->copy_buffers_[c - 1] + gf->frame_size_;
    }

    // Reserve deques
    for (size_t i = 0; i < gf->channels_; i++) {
        deque_reserve(&gf->input_buffers_[i],
                      8 * gf->frame_size_ * sizeof(float));
        deque_reserve(&gf->output_buffers_[i],
                      8 * gf->frame_size_ * sizeof(float));
    }

    return gf;
}

void vst3_graph_destroy(void *data)
{
    delete static_cast<VST3GraphFilter *>(data);
}

void vst3_graph_update(void *data, obs_data_t *settings)
{
    UNUSED_PARAMETER(settings);
    auto *gf = static_cast<VST3GraphFilter *>(data);
    // Graph nodes are managed via properties panel
    gf->bypass_.store(false, std::memory_order_relaxed);
}

struct obs_audio_data *vst3_graph_filter_audio(
    void *data, struct obs_audio_data *audio)
{
    auto *gf = static_cast<VST3GraphFilter *>(data);
    if (!gf || gf->bypass_.load(std::memory_order_relaxed)) {
        return audio;
    }

    auto &graph = gf->graph_;
    size_t segment_size = gf->frame_size_ * sizeof(float);

    // Push input data
    for (size_t i = 0; i < gf->channels_; i++) {
        deque_push_back(&gf->input_buffers_[i],
                        audio->data[i],
                        audio->frames * sizeof(float));
    }

    // Process segments
    while (gf->input_buffers_[0].size >= segment_size) {
        // Pop from input to temp
        for (size_t i = 0; i < gf->channels_; i++) {
            deque_pop_front(&gf->input_buffers_[i],
                            gf->copy_buffers_[i],
                            segment_size);
        }

        // Process through graph
        const float *input_ptrs[8];
        float *output_ptrs[8];
        for (size_t i = 0; i < gf->channels_; i++) {
            input_ptrs[i] = gf->copy_buffers_[i];
            output_ptrs[i] = gf->copy_buffers_[i];
        }

        // Temporary output buffer for graph output
        float *graph_output[8];
        float graph_buf[8 * 480];
        for (size_t i = 0; i < gf->channels_; i++) {
            graph_output[i] = graph_buf + i * 480;
        }

        graph->processChain(
            input_ptrs, graph_output,
            (uint32_t)gf->frame_size_,
            (uint32_t)gf->channels_,
            (uint32_t)gf->sample_rate_);

        // Push to output deque
        for (size_t i = 0; i < gf->channels_; i++) {
            deque_push_back(&gf->output_buffers_[i],
                            graph_output[i],
                            segment_size);
        }
    }

    // Return output
    size_t out_size = audio->frames * sizeof(float);
    if (gf->output_buffers_[0].size < out_size) {
        return nullptr;
    }

    da_resize(gf->output_data_, out_size * gf->channels_);
    for (size_t i = 0; i < gf->channels_; i++) {
        gf->output_audio_.data[i] =
            (uint8_t *)&gf->output_data_.array[i * out_size];
        deque_pop_front(&gf->output_buffers_[i],
                        gf->output_audio_.data[i], out_size);
    }
    gf->output_audio_.frames = audio->frames;
    gf->output_audio_.timestamp = audio->timestamp;
    return &gf->output_audio_;
}

obs_properties_t *vst3_graph_properties(void *data)
{
    auto *gf = static_cast<VST3GraphFilter *>(data);
    obs_properties_t *props = obs_properties_create();

    if (gf && gf->graph_) {
        // Node list with enable/disable and reorder
        obs_property_t *node_list = obs_properties_add_list(
            props, "graph_nodes", "Nodes",
            OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

        for (size_t i = 0; i < gf->graph_->nodeCount(); i++) {
            auto *node = gf->graph_->getNode(i + 1);
            if (node) {
                std::string label =
                    (node->enabled_ ? "[ON] " : "[OFF] ")
                    + node->name_;
                char id_str[32];
                snprintf(id_str, sizeof(id_str), "%zu",
                         node->id_);
                obs_property_list_add_string(
                    node_list, label.c_str(), id_str);
            }
        }
    }

    // Add node button (opens VST3 selector)
    obs_properties_add_button2(
        props, "graph_add_node", "Add VST3 Plugin...",
        [](obs_properties_t *p, obs_property_t *prop,
           void *data_) -> bool {
            UNUSED_PARAMETER(p);
            UNUSED_PARAMETER(prop);
            UNUSED_PARAMETER(data_);
            blog(LOG_INFO,
                 "[VST3 Graph] Add node requested");
            return true;
        }, data);

    // Remove node button
    obs_properties_add_button2(
        props, "graph_remove_node", "Remove Selected",
        [](obs_properties_t *p, obs_property_t *prop,
           void *data_) -> bool {
            UNUSED_PARAMETER(p);
            UNUSED_PARAMETER(prop);
            UNUSED_PARAMETER(data_);
            blog(LOG_INFO,
                 "[VST3 Graph] Remove node requested");
            return true;
        }, data);

    // Save/Load graph config
    obs_properties_add_button2(
        props, "graph_save", "Save Chain Config...",
        [](obs_properties_t *p, obs_property_t *prop,
           void *data_) -> bool {
            UNUSED_PARAMETER(p);
            UNUSED_PARAMETER(prop);
            auto *gf2 =
                static_cast<VST3GraphFilter *>(data_);
            if (gf2) {
                gf2->graph_->saveToJson("vst3graph.json");
            }
            return true;
        }, data);

    obs_properties_add_button2(
        props, "graph_load", "Load Chain Config...",
        [](obs_properties_t *p, obs_property_t *prop,
           void *data_) -> bool {
            UNUSED_PARAMETER(p);
            UNUSED_PARAMETER(prop);
            auto *gf2 =
                static_cast<VST3GraphFilter *>(data_);
            if (gf2) {
                gf2->graph_->loadFromJson("vst3graph.json");
            }
            return true;
        }, data);

    return props;
}

void vst3_graph_save(void *data, obs_data_t *settings)
{
    auto *gf = static_cast<VST3GraphFilter *>(data);
    if (!gf || !gf->graph_) return;

    // Serialize graph state to settings
    std::string json = gf->graph_->toJsonString();
    obs_data_set_string(settings, "graph_config", json.c_str());
}

void register_vst3_graph_source()
{
    struct obs_source_info vst3_graph_filter = {};
    vst3_graph_filter.id = "vst3_graph_filter";
    vst3_graph_filter.type = OBS_SOURCE_TYPE_FILTER;
    vst3_graph_filter.output_flags = OBS_SOURCE_AUDIO;
    vst3_graph_filter.get_name = vst3_graph_name;
    vst3_graph_filter.create = vst3_graph_create;
    vst3_graph_filter.destroy = vst3_graph_destroy;
    vst3_graph_filter.update = vst3_graph_update;
    vst3_graph_filter.filter_audio = vst3_graph_filter_audio;
    vst3_graph_filter.get_properties = vst3_graph_properties;
    vst3_graph_filter.save = vst3_graph_save;
    obs_register_source(&vst3_graph_filter);
}
