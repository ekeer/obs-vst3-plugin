/******************************************************************************
    Copyright (C) 2026 ekeer <ekeer@github.com>
    This file is part of obs-vst3.
    It uses the Steinberg VST3 SDK, which is licensed under MIT license.
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <functional>
#include <obs-module.h>
#include <util/deque.h>

class VST3Plugin;

/**
 * VST3GraphNode - A single node in the plugin processing graph.
 * Each node wraps a VST3Plugin instance with its own input/output buffers.
 */
struct VST3GraphNode {
    size_t id_;
    std::shared_ptr<VST3Plugin> plugin_;
    bool enabled_{true};
    std::string name_;
    
    // Audio buffers for this node
    std::vector<struct deque> input_buffers_;
    std::vector<struct deque> output_buffers_;
    float **copy_buffers_{nullptr};
    size_t channels_{0};
    size_t frame_size_{0};
    uint32_t sample_rate_{0};
    
    VST3GraphNode(size_t id, std::shared_ptr<VST3Plugin> plugin);
    ~VST3GraphNode();
    
    // No copy, only move
    VST3GraphNode(const VST3GraphNode&) = delete;
    VST3GraphNode& operator=(const VST3GraphNode&) = delete;
    VST3GraphNode(VST3GraphNode&& other) noexcept;
    VST3GraphNode& operator=(VST3GraphNode&& other) noexcept;
    
    bool process(uint32_t frames);
    void reset();
    
    size_t bufferSize() const;
};

/**
 * VST3Graph - A directed graph of VST3 plugin processing nodes.
 * Supports serial chains and can be extended for parallel routing.
 * Audio flows sequentially: input -> Node[0] -> Node[1] -> ... -> output
 */
class VST3Graph {
public:
    VST3Graph();
    ~VST3Graph();
    
    // Node management
    size_t addNode(std::shared_ptr<VST3Plugin> plugin);
    bool removeNode(size_t node_id);
    bool moveNode(size_t node_id, size_t new_index);
    void clear();
    
    // Node access
    size_t nodeCount() const { return nodes_.size(); }
    VST3GraphNode* getNode(size_t node_id);
    const VST3GraphNode* getNode(size_t node_id) const;
    
    // Audio processing (audio thread - lock free when possible)
    bool processChain(const float *const *input_data,
                      float **output_data,
                      uint32_t frames,
                      uint32_t channels,
                      uint32_t sample_rate);
    
    // Serialization
    bool saveToJson(const std::string &path);
    bool loadFromJson(const std::string &path);
    std::string toJsonString();
    bool fromJsonString(const std::string &json);
    
    // Latency
    uint32_t getTotalLatency() const;
    
    // Configuration
    void setSampleRate(uint32_t sr) { sample_rate_ = sr; }
    void setChannels(uint32_t ch) { channels_ = ch; }
    void setFrameSize(size_t fs) { frame_size_ = fs; }
    
private:
    std::vector<VST3GraphNode> nodes_;
    size_t next_id_{1};
    uint32_t sample_rate_{48000};
    uint32_t channels_{2};
    size_t frame_size_{480};
    
    // Temporary buffers for inter-node routing
    float *temp_buffer_{nullptr};
    size_t temp_buffer_size_{0};
    
    void ensureTempBuffer(size_t needed);
};

/**
 * VST3GraphFilter - A specialized OBS filter type that hosts a VST3Graph
 * instead of a single VST3 plugin. Allows creating plugin chains/link
 * within a single OBS filter instance.
 */
struct VST3GraphFilter {
    std::unique_ptr<VST3Graph> graph_;
    obs_source_t *context_{nullptr};
    
    struct deque input_buffers_[8];
    struct deque output_buffers_[8];
    struct deque info_buffer_;
    float *copy_buffers_[8]{};
    
    uint32_t sample_rate_{48000};
    size_t channels_{2};
    size_t frame_size_{480};
    speaker_layout layout_{SPEAKERS_STEREO};
    
    int64_t running_sample_count_{0};
    uint64_t last_timestamp_{0};
    uint64_t latency_{0};
    
    struct obs_audio_data output_audio_;
    DARRAY(float) output_data_;
    
    std::atomic<bool> bypass_{false};
    std::atomic_flag init_in_progress_ = ATOMIC_FLAG_INIT;
    
    VST3GraphFilter();
    ~VST3GraphFilter();
};

/* ---------- Graph filter OBS source registration ---------- */
void register_vst3_graph_source();
void *vst3_graph_create(obs_data_t *settings, obs_source_t *filter);
void vst3_graph_destroy(void *data);
void vst3_graph_update(void *data, obs_data_t *settings);
struct obs_audio_data *vst3_graph_filter_audio(void *data,
                                                struct obs_audio_data *audio);
obs_properties_t *vst3_graph_properties(void *data);
void vst3_graph_save(void *data, obs_data_t *settings);
