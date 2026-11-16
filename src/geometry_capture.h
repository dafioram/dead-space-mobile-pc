#pragma once
#include "geometry_diagnostics.h"
#include <deque>
#include <vector>
#include <unordered_map>

namespace geometry {
constexpr size_t history_frames = 120, post_frames = 30, frame_candidates = 16, capture_candidates = 12;
struct Frame {
    uint64_t number=0, scene=0;
    size_t draws=0, scanned=0, limited=0;
    std::vector<Summary> candidates;
};
struct Line { float x0,y0,x1,y1; uint32_t color; };

// Single render-thread owner, like the GLES state it observes. Commands are
// issued between native frames. Only copied summaries survive observe().
class Capture {
    bool enabled_=false, in_frame_=false, pending_=false, focus_seen_=false, ready_=false;
    uint64_t frame_=0, scene_=0, mark_=0, capture_=0, focus_=0;
    uint64_t last_mark_=0;
    Frame current_;
    std::deque<Frame> history_;
    std::deque<std::string> output_;
    std::vector<Summary> choices_;
    std::unordered_map<uint64_t,uint32_t> occurrences_;
    std::vector<uint64_t> warned_;
    std::vector<Line> lines_;
    size_t inspected_=0;
    std::string (*symbolize_)(uint32_t)=nullptr;
    void finish_capture(const char* reason);
    void message(const std::string& text);
public:
    bool enabled() const { return enabled_; }
    bool focused() const { return focus_!=0; }
    bool focus_seen() const { return focus_seen_; }
    bool pending() const { return pending_; }
    bool ready() const { return ready_; }
    uint64_t frame() const { return frame_; }
    uint64_t scene() const { return scene_; }
    size_t retained_frames() const { return history_.size(); }
    size_t queued_lines() const { return output_.size(); }
    size_t choices() const { return choices_.size(); }
    const std::vector<Line>& lines() const { return lines_; }
    const Frame& current() const { return current_; }
    void symbolizer(std::string (*callback)(uint32_t)) { symbolize_=callback; }
    std::string command(const std::string& normalized);
    std::string mark();
    void begin_frame();
    void observe(const Draw& draw, int width, int height);
    void end_frame();
    void scene_changed();
    void finish();
    std::vector<std::string> drain(size_t limit=24);
};

Capture& capture();
// Application glue; emits queued diagnostic text only, never changes draws.
void capture_log(bool all=false);
}  // namespace geometry
