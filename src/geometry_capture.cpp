#include "geometry_capture.h"
#include "log.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace geometry {
namespace {
std::string hex(uint64_t value) { char text[24];std::snprintf(text,sizeof text,"%016llx",(unsigned long long)value);return text; }
std::string brief(const Summary& s) {
    std::ostringstream out;
    out<<"sig="<<hex(s.signature)<<" sample-frame="<<s.frame<<" scene="<<s.scene<<" draw="<<s.ordinal<<" data="<<hex(s.digest)<<" caller="<<hex(s.caller)
       <<" renderer="<<(s.state.native?"native":"legacy")<<" mode="<<s.mode<<" count="<<s.count
       <<" first="<<s.first<<" tex="<<s.state.texture[0]<<" framebuffer="<<s.state.framebuffer<<" idx="<<(s.indexed?"indexed":"arrays")
       <<" range="<<s.min_index<<".."<<s.max_index<<" inspected="<<s.inspected<<" tris="<<s.triangle_count
       <<" edge-eye="<<s.longest.edge<<" aspect="<<s.thinnest.aspect<<" degenerates="<<s.degenerate
       <<" flags="<<flags(s)<<" attribute-read-mask="<<s.unreadable_attributes<<" attribute-layout-mask="<<s.bad_attribute_layouts<<" rank="<<s.rank;
    return out.str();
}
std::string array_text(const char* name,const ArrayInfo& a) {
    std::ostringstream out;out<<name<<" enabled="<<a.enabled<<" ptr="<<hex(a.pointer)<<" buffer="<<a.buffer
      <<" generation="<<a.generation<<" components="<<a.components<<" type="<<a.type<<" stride="<<a.stride;
    return out.str();
}
template<class Values> std::string values(const Values& numbers) {
    std::ostringstream out;out.precision(9);for (auto n:numbers) out<<' '<<n;return out.str();
}
bool better(const Summary& a,const Summary& b) {
    return a.rank!=b.rank?a.rank>b.rank:a.signature<b.signature;
}
void retain(std::vector<Summary>& list,Summary summary,size_t maximum) {
    if(list.size()==maximum && !better(summary,list.back()))return;
    auto at=std::lower_bound(list.begin(),list.end(),summary,better);
    list.insert(at,std::move(summary));if(list.size()>maximum)list.pop_back();
}
}
void Capture::message(const std::string& text) {
    // Includes explicit overflow rather than unbounded growth if the host stops draining.
    if(output_.size()<2048)output_.push_back("[geometry] "+text);
    else if(output_.size()==2048)output_.push_back("[geometry] OUTPUT LIMIT: additional diagnostic lines omitted");
}
std::string Capture::command(const std::string& line) {
    if(line=="/geom" || line=="/geom_status") {
        return std::string("geometry capture ")+(enabled_?"ON":"OFF")+"; frame="+std::to_string(frame_)+
          " scene="+std::to_string(scene_)+" history="+std::to_string(history_.size())+
          " pending="+(pending_?"yes":"no")+" candidates="+std::to_string(choices_.size())+"; /geom on|mark|focus N|off; F4 marks while armed";
    }
    if(line=="/geom_on") {
        if(enabled_)return "geometry capture already ON; F4 to mark";
        enabled_=true;history_.clear();choices_.clear();focus_=0;last_mark_=0;ready_=false;
        message("ARMED read-only shared-GLES observer; history=120 post=30 top/frame=16 candidates=12; per-draw indices<=32768 per-frame<=196608; samples only, owner=unknown; no geometry suppressed");
        return "geometry capture ON (both renderers); close console, F4 when a streak is visible; extra CPU work while armed";
    }
    if(line=="/geom_mark")return mark();
    if(line=="/geom_off") {
        if(pending_)finish_capture("disabled-before-post-window-completed");
        enabled_=false;focus_=0;choices_.clear();history_.clear();lines_.clear();ready_=false;
        message("DISARMED; observation/highlight stopped; game rendering and saves unchanged");
        return "geometry capture OFF; queued report continues into deadspace.log";
    }
    constexpr const char* prefix="/geom_focus_";
    if(line.rfind(prefix,0)==0) {
        unsigned n=0;auto start=line.data()+std::char_traits<char>::length(prefix);
        auto [end,error]=std::from_chars(start,line.data()+line.size(),n);
        if(error!=std::errc{}||end!=line.data()+line.size())return "focus needs an integer 0..12 (0 clears highlight)";
        if(!n){focus_=0;lines_.clear();return "geometry highlight OFF; capture remains armed";}
        if(!enabled_||pending_||n>choices_.size())return "no such captured candidate yet; /geom on, close console, F4 and wait one second";
        focus_=choices_[n-1].signature;
        message("FOCUS candidate="+std::to_string(n)+" sig="+hex(focus_)+"; yellow=longest sampled triangle, magenta=thinnest; x-ray overlay, not actual visibility; submission signature may alias/reuse, not owner identity");
        return "highlighting candidate "+std::to_string(n)+"; close console; /geom focus 0 clears; no draw hidden";
    }
    return "usage: /geom on | mark | status | focus N | off (spaces or underscores)";
}
std::string Capture::mark() {
    if(!enabled_)return "geometry capture is OFF; /geom on first";
    if(pending_)return "geometry marker already pending; wait one second";
    if(last_mark_ && frame_-last_mark_<60)return "geometry marker cooldown: wait one second";
    if(output_.size()>512)return "previous geometry report still draining; wait before another marker";
    pending_=true;ready_=false;mark_=frame_;last_mark_=frame_;++capture_;choices_.clear();focus_=0;
    message("MARK capture="+std::to_string(capture_)+" frame="+std::to_string(mark_)+" scene="+std::to_string(scene_)+"; retaining recent frames plus next 30; candidate ranking is heuristic, not a corruption verdict");
    return "geometry marker "+std::to_string(capture_)+" recorded; wait one second for candidate report in deadspace.log";
}
void Capture::begin_frame() {
    ++frame_;in_frame_=true;current_={};current_.number=frame_;current_.scene=scene_;
    inspected_=0;occurrences_.clear();lines_.clear();focus_seen_=false;
}
void Capture::observe(const Draw& draw,int width,int height) {
    if(!enabled_||!in_frame_)return;
    ++current_.draws;
    if(current_.draws>2048){++current_.limited;return;}
    size_t budget=inspected_>=max_frame_indices?0:std::min(max_draw_indices,max_frame_indices-inspected_);
    if(!budget){++current_.limited;return;}
    Summary summary=inspect(draw,budget);
    summary.frame=frame_;summary.scene=scene_;summary.ordinal=current_.draws;
    inspected_+=summary.inspected;current_.scanned=inspected_;
    if(summary.limited)++current_.limited;
    // Repeated uses of scratch vertices in one frame are separate occurrences.
    // Draw-order changes can still change this association; never call it a mesh ID.
    if(occurrences_.size()<2048) {
        uint32_t occurrence=occurrences_[summary.signature]++;
        summary.signature=(summary.signature^occurrence)*1099511628211ull;
    } else {summary.limited=true;++current_.limited;}
    if((summary.bad_layout || summary.bad_index_span || summary.unreadable || summary.nonfinite || summary.unreadable_attributes || summary.bad_attribute_layouts) &&
       warned_.size()<8 && std::find(warned_.begin(),warned_.end(),summary.signature)==warned_.end()) {
        warned_.push_back(summary.signature);
        // Preserve the first severe observations before the original GL path
        // dereferences its input. No skip, clamp, fix or guest write follows.
        LOGW("[geometry] CHECK frame=%llu scene=%llu %s; read-only diagnostic, original draw unchanged; at most 8 unique checks/scene",
             (unsigned long long)frame_,(unsigned long long)scene_,brief(summary).c_str());
    }
    if(summary.signature==focus_) {
        focus_seen_=true;
        for(auto [triangle,color]:{std::pair{&summary.longest,0xffff00ffu},{&summary.thinnest,0xff40ffffu}}) {
            if(!triangle->valid || summary.state.framebuffer!=0)continue;
            for(int edge=0;edge<3;edge++) {
                float line[4];
                if(lines_.size()<6 && screen_edge(triangle->clip[edge],triangle->clip[(edge+1)%3],summary.state,width,height,line))
                    lines_.push_back({line[0],line[1],line[2],line[3],color});
            }
        }
    }
    // Depth-disabled perspective effects can themselves explain streaks through
    // walls. Do not discard them just because their depth test is off.
    bool severe=summary.bad_layout||summary.bad_index_span||summary.unreadable||summary.nonfinite||summary.unreadable_attributes||summary.bad_attribute_layouts;
    if((summary.state.depth_test || summary.state.projection[11]!=0 || severe) && (summary.inspected||summary.bad_layout||summary.unsupported))
        retain(current_.candidates,std::move(summary),frame_candidates);
}
void Capture::end_frame() {
    if(!in_frame_)return;
    in_frame_=false;
    if(!enabled_)return;
    history_.push_back(std::move(current_));
    size_t keep=pending_?history_frames+post_frames:history_frames;
    while(history_.size()>keep)history_.pop_front();
    if(pending_ && frame_>=mark_+post_frames)finish_capture("complete");
}
void Capture::finish_capture(const char* reason) {
    if(!pending_)return;
    pending_=false;
    message("REPORT BEGIN capture="+std::to_string(capture_)+" marker-frame="+std::to_string(mark_)+" scene="+std::to_string(scene_)+" reason="+reason+
            "; eye-space not world-space; mapped guest pages do NOT establish allocation ownership; top candidates only, complete data is not retained");
    // Top geometry over the interval, keeping the latest observation when a key
    // is repeated at an equal rank. No guest pointers are dereferenced here.
    std::vector<Summary> unique;
    for(const auto& frame:history_) {
        for(size_t i=0;i<std::min(size_t(2),frame.candidates.size());i++)
            message("history frame="+std::to_string(frame.number)+" scene="+std::to_string(frame.scene)+" draws="+std::to_string(frame.draws)+
                    " scanned="+std::to_string(frame.scanned)+" limited="+std::to_string(frame.limited)+" "+brief(frame.candidates[i]));
        for(const auto& summary:frame.candidates) {
            auto it=std::find_if(unique.begin(),unique.end(),[&](const auto& s){return s.signature==summary.signature;});
            if(it!=unique.end()) {if(summary.rank>=it->rank)*it=summary;}
            else if(unique.size()<2400)unique.push_back(summary);
        }
    }
    std::sort(unique.begin(),unique.end(),better);
    if(unique.size()>capture_candidates)unique.resize(capture_candidates);
    choices_=std::move(unique);
    ready_=true;
    for(size_t i=0;i<choices_.size();i++) {
        const auto& s=choices_[i];std::string tag="candidate="+std::to_string(i+1)+" sig="+hex(s.signature)+" ";
        uint64_t first=0,last=0,digest=0;size_t observed=0,changes=0;
        for(const auto& frame:history_)for(const auto& sample:frame.candidates)if(sample.signature==s.signature) {
            if(!observed)first=frame.number;
            else if(sample.digest!=digest)++changes;
            ++observed;last=frame.number;digest=sample.digest;
        }
        message(tag+brief(s)+" owner=unknown signature-is-not-object-identity");
        if(symbolize_)message(tag+"caller-symbol="+symbolize_(s.caller));
        message(tag+"retained-observations="+std::to_string(observed)+" first-frame="+std::to_string(first)+" last-frame="+std::to_string(last)+
          " geometry-digest-changes="+std::to_string(changes)+"; first/last refer to ranked retention, NOT object creation/destruction");
        message(tag+array_text("position",s.position));message(tag+array_text("normal",s.normal));
        message(tag+array_text("color",s.color));message(tag+array_text("uv0",s.uv));
        message(tag+"indices ptr="+hex(s.index_pointer)+" buffer="+std::to_string(s.index_buffer)+" generation="+std::to_string(s.index_generation)+
          " allocation-bound="+(s.index_buffer?"shadow-buffer":"unknown-guest-allocation")+" position-allocation-bound="+(s.position.buffer?"shadow-buffer":"unknown-guest-allocation"));
        message(tag+"modelview"+values(s.state.modelview));message(tag+"projection"+values(s.state.projection));
        message(tag+"viewport"+values(s.state.viewport)+" blend="+std::to_string(s.state.blend)+" src="+std::to_string(s.state.blend_src)+" dst="+std::to_string(s.state.blend_dst)+
          " depth="+std::to_string(s.state.depth_test)+" write="+std::to_string(s.state.depth_write)+" func="+std::to_string(s.state.depth_func)+
          " cull="+std::to_string(s.state.cull)+" face="+std::to_string(s.state.cull_mode)+" front="+std::to_string(s.state.front_face)+
          " alpha="+std::to_string(s.state.alpha_test)+" alpha-func="+std::to_string(s.state.alpha_func)+" alpha-ref="+std::to_string(s.state.alpha_ref)+" fog="+std::to_string(s.state.fog));
        message(tag+"texture0-enabled="+std::to_string(s.state.texture_enabled[0])+" env="+std::to_string(s.state.texture_env[0])+
          " texture1="+std::to_string(s.state.texture[1])+" enabled="+std::to_string(s.state.texture_enabled[1])+" env="+std::to_string(s.state.texture_env[1])+
          " constant-color"+values(s.state.color)+" fixed-lighting="+std::to_string(s.state.lighting));
        message(tag+"scissor-enabled="+std::to_string(s.state.scissor_on)+" box"+values(s.state.scissor)+" polygon-offset="+std::to_string(s.state.polygon_offset)+
          " factor="+std::to_string(s.state.polygon_factor)+" units="+std::to_string(s.state.polygon_units));
        message(tag+"local-min"+values(s.local_min)+" local-max"+values(s.local_max)+" eye-min"+values(s.eye_min)+" eye-max"+values(s.eye_max));
        for(auto [sample,name]:{std::pair{&s.longest,"longest"},{&s.thinnest,"thinnest"}}) {
            if(!sample->valid)continue;
            message(tag+name+" triangle="+std::to_string(sample->number)+" indices"+values(sample->indices)+" edge="+std::to_string(sample->edge)+" aspect="+std::to_string(sample->aspect));
            for(int k=0;k<3;k++)message(tag+name+" vertex="+std::to_string(k)+" local"+values(sample->local[k])+" eye"+values(sample->eye[k])+" clip"+values(sample->clip[k]));
        }
    }
    message("REPORT END capture="+std::to_string(capture_)+" candidates="+std::to_string(choices_.size())+"; /geom focus 1.."+std::to_string(choices_.size())+
            " outlines sampled triangles only; save log before next launch; unusual size/aspect alone is NOT proof of corruption");
}
void Capture::scene_changed() {
    if(pending_)finish_capture("scene-change-before-post-window-completed");
    ++scene_;history_.clear();choices_.clear();focus_=0;lines_.clear();last_mark_=0;warned_.clear();ready_=false;
    if(in_frame_){current_={};current_.number=frame_;current_.scene=scene_;inspected_=0;occurrences_.clear();}
    if(enabled_)message("SCENE boundary="+std::to_string(scene_)+"; history/focus reset, observation remains armed; no mesh ownership inferred");
}
void Capture::finish() { if(pending_)finish_capture("shutdown-before-post-window-completed"); }
std::vector<std::string> Capture::drain(size_t limit) {
    std::vector<std::string> out;
    while(!output_.empty()&&out.size()<limit){out.push_back(std::move(output_.front()));output_.pop_front();}
    return out;
}
Capture& capture() {static Capture instance;return instance;}
void capture_log(bool all) {
    for(const auto& text:capture().drain(all?4096:24))LOGI("%s",text.c_str());
}
}  // namespace geometry
