#pragma once

// Flush — Visage UI (native C++, GPU-rendered) — Pro-Q4 recreation
//
// Quality bar: FabFilter Pro-Q4. Dark near-black panel, neon accent glow,
// glassy knobs, vertical-bar / VU meters, an interactive EQ display (draggable
// nodes) over a high-quality FFT spectrum, and a header with phase mode +
// latency. All drawing is anti-aliased and driven at 120 Hz+ by the render loop
// in PluginEditor (on-demand invalidation = uncapped/host-vsync).
//
// NOTE: this is written against the verified Visage API (Frame, Canvas::fill/
// circle/arc/text, MouseEvent, addChild) but has not been compile-verified in
// this environment (no cmake/visage build). Visual fine-tuning (arc orientation,
// exact radii/colors) is expected at first build.

#include <visage/ui.h>
#include <visage/graphics.h>
#include "BinaryData.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace {
    constexpr double kPi = 3.14159265358979323846;

    namespace C {
        constexpr unsigned int bg0       = 0xff101216;
        constexpr unsigned int bg1       = 0xff1b1e24;
        constexpr unsigned int surface   = 0xff23272f;
        constexpr unsigned int surface2  = 0xff2a3038;
        constexpr unsigned int stroke    = 0xff0b0d10;
        constexpr unsigned int hairline  = 0xff343b45;
        constexpr unsigned int accent    = 0xff3ea6ff;
        constexpr unsigned int accentSoft= 0xff8fcbff;
        constexpr unsigned int text      = 0xffd4dbe3;
        constexpr unsigned int textDim   = 0xff9099a5;
        constexpr unsigned int label     = 0xff6c7480;
        constexpr unsigned int green     = 0xff3ddc84;
        constexpr unsigned int yellow    = 0xffffd166;
        constexpr unsigned int red       = 0xffff5c5c;
    }

    inline float clamp (float v, float lo, float hi) { return std::max (lo, std::min (hi, v)); }

    inline visage::Font makeFont (float size, float dpi)
    {
        const auto* d = reinterpret_cast<const unsigned char*> (flush_BinaryData::LatoRegular_ttf);
        return visage::Font (size, d, flush_BinaryData::LatoRegular_ttfSize, dpi);
    }
}

// ============================================================ Glassy knob ====
class FlushKnob : public visage::Frame
{
public:
    FlushKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& label)
        : param_ (apvts.getParameter (id)), label_ (label) {}

    void init() override
    {
        const float dpi = std::max (1.0f, dpiScale());
        labelFont_ = makeFont (9.0f, dpi);
        valueFont_ = makeFont (12.0f, dpi);
    }

    void draw (visage::Canvas& canvas) override
    {
        const float cx = width() * 0.5f;
        const float cy = width() * 0.5f;
        const float r  = width() * 0.40f;
        const float value = param_ ? param_->getValue() : 0.0f;

        // Machined ring + body.
        canvas.setColor (C::hairline);
        canvas.circle (cx - r - 2.0f, cy - r - 2.0f, (r + 2.0f) * 2.0f);
        canvas.setColor (C::surface);
        canvas.circle (cx - r, cy - r, r * 2.0f);
        canvas.setColor (C::stroke);
        canvas.circle (cx - r + 1.5f, cy - r + 1.5f, (r - 3.0f) * 2.0f);

        // Glass catch-light.
        canvas.setColor (0x2e565d6b);
        canvas.circle (cx - r * 0.55f, cy - r * 0.72f, r * 0.55f);

        // Inactive track (full 270° sweep, gap at the bottom).
        canvas.setColor (C::surface2);
        canvas.arc (cx - r, cy - r, r * 2.0f, 3.0f, (float)kPi, -(float)(kPi * 1.5f), true);

        // Value arc (270° sweep, gap at the bottom).
        canvas.setColor (C::accent);
        canvas.arc (cx - r, cy - r, r * 2.0f, 3.0f, (float)kPi, -(float)(kPi * 1.5 * value), true);

        // Pointer dot.
        const double ang = (-135.0 + 270.0 * value) * (kPi / 180.0);
        const float px = (float)(cx + r * 0.72 * std::sin (ang));
        const float py = (float)(cy - r * 0.72 * std::cos (ang));
        canvas.setColor (C::accentSoft);
        canvas.circle (px - 3.0f, py - 3.0f, 6.0f);

        // Label + value readout.
        canvas.setColor (C::label);
        canvas.text (label_.toRawUTF8(), labelFont_, visage::Font::kCenter, 0.0f, height() - 34.0f, width(), 12.0f);
        canvas.setColor (C::text);
        canvas.text (param_ ? param_->getText (value, 0).toRawUTF8() : "",
                     valueFont_, visage::Font::kCenter, 0.0f, height() - 22.0f, width(), 16.0f);
    }

    void mouseDown (const visage::MouseEvent& e) override
    {
        if (!param_ || !e.isLeftButtonCurrentlyDown()) return;
        dragging_ = true;
        dragStart_ = e.position.y;
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastClick_ < 400)                          // double-click resets
            param_->setValueNotifyingHost (param_->getDefaultValue());
        lastClick_ = now;
        redraw();
    }

    void mouseDrag (const visage::MouseEvent& e) override
    {
        if (!dragging_ || !param_) return;
        const float dy = e.position.y - dragStart_;
        dragStart_ = e.position.y;
        const double sens = e.isShiftDown() ? 0.002 : 0.01;  // shift = fine
        param_->setValueNotifyingHost ((float)juce::jlimit (0.0, 1.0, param_->getValue() - dy * sens));
        redraw();
    }

    void mouseUp (const visage::MouseEvent&) override { dragging_ = false; }

private:
    juce::AudioProcessorParameter* param_ = nullptr;
    juce::String label_;
    visage::Font labelFont_, valueFont_;
    bool dragging_ = false;
    float dragStart_ = 0.0f;
    juce::uint32 lastClick_ = 0;
};

// ====================================================== Bar / VU meter =======
class FlushMeter : public visage::Frame
{
public:
    FlushMeter (juce::AudioProcessorValueTreeState& apvts,
                const std::atomic<float>& level, const std::atomic<float>& peak)
        : apvts_ (apvts), level_ (level), peak_ (peak) {}

    void init() override
    {
        const float dpi = std::max (1.0f, dpiScale());
        labelFont_ = makeFont (8.0f, dpi);
    }

    void draw (visage::Canvas& canvas) override
    {
        const bool vu = (int)*apvts_.getRawParameterValue ("meter_mode") == 1;
        const double db = level_.load();
        if (vu) drawVU (canvas, db); else drawBar (canvas, db);
    }

private:
    void drawBar (visage::Canvas& canvas, double db)
    {
        const float x = 4.0f, w = width() - 8.0f;
        const float h = height() - 24.0f;
        const float y0 = 2.0f;

        canvas.setColor (C::surface);
        canvas.rectangle (x, y0, w, h);

        const double lvl = clamp ((float)((db + 60.0) / 60.0), 0.0f, 1.0f);
        const float filled = h * (float)lvl;

        // Segmented fill (green -> yellow -> red).
        auto zone = [&](double lo, double hi, unsigned int colour) {
            const float fLo = h * (float)((lo + 60.0) / 60.0);
            const float fHi = h * (float)((hi + 60.0) / 60.0);
            const float top = std::min (filled, fHi);
            if (top > fLo) {
                canvas.setColor (colour);
                canvas.rectangle (x, y0 + h - top, w, top - fLo);
            }
        };
        zone (-60.0, -18.0, C::green);
        zone (-18.0, -6.0,  C::yellow);
        zone (-6.0,   0.0,  C::red);

        // Peak hold tick.
        const double pk = clamp ((float)((peak_.load() + 60.0) / 60.0), 0.0f, 1.0f);
        canvas.setColor (C::accentSoft);
        canvas.rectangle (x, y0 + h - h * (float)pk, w, 2.0f);

        // Frame + labels.
        canvas.setColor (C::hairline);
        canvas.rectangleBorder (x, y0, w, h, 1.0f);
        canvas.setColor (C::label);
        for (double tick : { -60.0, -30.0, -18.0, -6.0, 0.0 }) {
            const float ty = y0 + h - h * (float)((tick + 60.0) / 60.0);
            canvas.text (juce::String ((int)tick).toRawUTF8(), labelFont_, visage::Font::kRight, 0.0f, ty - 5.0f, x - 2.0f, 10.0f);
        }

        // Numeric level readout (dBFS) under the meter.
        canvas.setColor (C::text);
        canvas.text (juce::String (db, 1).toRawUTF8(), labelFont_, visage::Font::kCenter,
                     0.0f, height() - 12.0f, width(), 10.0f);
    }

    void drawVU (visage::Canvas& canvas, double db)
    {
        const float cx = width() * 0.5f;
        const float cy = height() - 40.0f;
        const float r  = std::min (width(), height() - 44.0f) * 0.46f;

        // Arc scale.
        canvas.setColor (C::surface);
        canvas.arc (cx - r, cy - r, r * 2.0f, 5.0f, (float)(kPi * 0.75), (float)(kPi * 1.5), false);

        // Needle: -60..+5 dB mapped across the lower half arc.
        const double t = clamp ((float)((db + 60.0) / 65.0), 0.0f, 1.0f);
        const double ang = (0.25 - 0.5 * t) * kPi;      // 0.25pi .. -0.25pi
        canvas.setColor (C::text);
        const float nx = (float)(cx + r * 0.9 * std::cos (ang));
        const float ny = (float)(cy + r * 0.9 * std::sin (ang));
        canvas.circle (nx - 2.0f, ny - 2.0f, 4.0f);
        canvas.setColor (C::stroke);
        canvas.circle (cx - 3.0f, cy - 3.0f, 6.0f);

        canvas.setColor (C::label);
        canvas.text ("VU", labelFont_, visage::Font::kCenter, 0.0f, height() - 14.0f, width(), 12.0f);
    }

    juce::AudioProcessorValueTreeState& apvts_;
    const std::atomic<float>& level_;
    const std::atomic<float>& peak_;
    visage::Font labelFont_;
};

// ============================================== Gain-reduction meter ==========
class FlushGrMeter : public visage::Frame
{
public:
    explicit FlushGrMeter (const std::atomic<float>& gr) : gr_ (gr) {}

    void init() override
    {
        const float dpi = std::max (1.0f, dpiScale());
        label_font_ = makeFont (8.0f, dpi);
    }

    void draw (visage::Canvas& canvas) override
    {
        const float x = 4.0f, w = width() - 8.0f;
        const float y0 = 2.0f, h = height() - 24.0f;

        canvas.setColor (C::surface);
        canvas.rectangle (x, y0, w, h);

        // Downward GR bar: 0..-24 dB, hangs from the top.
        const float gr = clamp (gr_.load() / 24.0f, 0.0f, 1.0f);
        const float filled = h * gr;
        if (filled > 0.5f) {
            canvas.setColor (C::accent);
            canvas.rectangle (x, y0, w, filled);
            canvas.setColor (C::accentSoft);
            canvas.rectangle (x, y0 + filled, w, 2.0f);
        }

        canvas.setColor (C::hairline);
        canvas.rectangleBorder (x, y0, w, h, 1.0f);

        canvas.setColor (C::label);
        canvas.text ("GR", label_font_, visage::Font::kCenter, 0.0f, height() - 22.0f, width(), 10.0f);
        canvas.setColor (C::textDim);
        canvas.text (juce::String (gr_.load(), 1).toRawUTF8(), label_font_,
                     visage::Font::kCenter, 0.0f, height() - 12.0f, width(), 10.0f);
    }

private:
    const std::atomic<float>& gr_;
    visage::Font label_font_;
};

// ============================================== Click-to-cycle choice control ==
class FlushChoiceButton : public visage::Frame
{
public:
    FlushChoiceButton (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& label)
        : apvts_ (apvts), param_ (apvts.getParameter (id)), label_ (label) {}

    void init() override
    {
        const float dpi = std::max (1.0f, dpiScale());
        labelFont_ = makeFont (8.0f, dpi);
        valueFont_ = makeFont (11.0f, dpi);
    }

    void draw (visage::Canvas& canvas) override
    {
        canvas.setColor (C::surface);
        canvas.roundedRectangle (1.0f, 10.0f, width() - 2.0f, height() - 12.0f, 4.0f);
        canvas.setColor (C::hairline);
        canvas.roundedRectangleBorder (1.0f, 10.0f, width() - 2.0f, height() - 12.0f, 4.0f, 1.0f);

        canvas.setColor (C::label);
        canvas.text (label_.toRawUTF8(), labelFont_, visage::Font::kCenter, 0.0f, 1.0f, width(), 10.0f);
        canvas.setColor (C::accent);
        canvas.text (param_ ? param_->getCurrentValueAsText().toRawUTF8() : "",
                     valueFont_, visage::Font::kCenter, 0.0f, height() - 16.0f, width(), 14.0f);
    }

    void mouseDown (const visage::MouseEvent& e) override
    {
        if (!param_ || !e.isLeftButtonCurrentlyDown()) return;
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param_)) {
            const int n = choice->choices.size();
            choice->setValueNotifyingHost ((float)((choice->getIndex() + 1) % n) / std::max (1, n - 1));
        }
        redraw();
    }

private:
    juce::AudioProcessorValueTreeState& apvts_;
    juce::AudioProcessorParameter* param_ = nullptr;
    juce::String label_;
    visage::Font labelFont_, valueFont_;
};

// ============================================== EQ display (analyzer + nodes) ==
class FlushEqDisplay : public visage::Frame
{
public:
    explicit FlushEqDisplay (FlushAudioProcessor& p) : processor_ (p) {}

    void init() override
    {
        const float dpi = std::max (1.0f, dpiScale());
        labelFont_ = makeFont (8.0f, dpi);
    }

    void draw (visage::Canvas& canvas) override
    {
        // Plot area (leave margins for axis labels).
        const float px = 34.0f, py = 8.0f;
        const float pw = width() - px - 10.0f;
        const float ph = height() - py - 22.0f;

        canvas.setColor (C::surface);
        canvas.rectangle (px, py, pw, ph);

        // Spectrum analyzer (gradient-ish fill via column alpha falloff).
        if (*processor_.parameters.getRawParameterValue ("analyzer_on") > 0.5f) {
            const auto& mags = processor_.analyzerMagnitudes();
            const int N = (int)mags.size();
            const float colW = pw / N;
            const float rangeDb = { 60.0f, 90.0f, 120.0f }[(int)*processor_.parameters.getRawParameterValue ("analyzer_range")];
            for (int i = 0; i < N; ++i) {
                const float lvl = clamp (mags[i] / rangeDb + 1.0f, 0.0f, 1.0f);
                const float colH = ph * lvl;
                if (colH < 0.5f) continue;
                // Column alpha fades toward the top (cheap "gradient").
                const unsigned int a = (unsigned int)(0x60 + 0x80 * lvl);
                canvas.setColor ((a << 24) | (C::accent & 0x00ffffff));
                canvas.rectangle (px + i * colW, py + ph - colH, std::max (1.0f, colW - 0.5f), colH);
                // Bright 2 px top edge (Pro-Q-style peak "hairline").
                canvas.setColor (C::accentSoft);
                canvas.rectangle (px + i * colW, py + ph - colH, std::max (1.0f, colW - 0.5f), 2.0f);
            }
        }

        // Frequency grid (log).
        canvas.setColor (C::stroke);
        for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 }) {
            const float gx = px + pw * (float)(std::log10 (f / 20.0) / std::log10 (20000.0 / 20.0));
            canvas.rectangle (gx, py, 1.0f, ph);
            canvas.setColor (C::label);
            canvas.text ((juce::String ((int)f) + (f >= 1000 ? "k" : "")).toRawUTF8(), labelFont_,
                         visage::Font::kCenter, gx - 15.0f, py + ph + 2.0f, 30.0f, 12.0f);
            canvas.setColor (C::stroke);
        }

        // dB grid + EQ curve.
        const double range = { 3.0, 6.0, 12.0, 30.0 }[(int)*processor_.parameters.getRawParameterValue ("eq_scale")];
        const int curveN = 128;
        float prevX = px, prevY = py + ph * 0.5f;
        for (int i = 0; i <= curveN; ++i) {
            const double f = 20.0 * std::pow (20000.0 / 20.0, (double)i / curveN);
            const double db = processor_.eqResponseDb (f);
            const float cx = px + pw * (float)((double)i / curveN);
            const float cy = py + ph * 0.5f - ph * 0.5f * (float)(db / range);
            if (i > 0) {
                canvas.setColor (C::accent);
                // Line segment as a thin rect.
                const float x0 = std::min (prevX, cx), x1 = std::max (prevX, cx);
                const float y0 = std::min (prevY, cy), y1 = std::max (prevY, cy);
                canvas.rectangle (x0, y0, std::max (1.0f, x1 - x0), std::max (1.0f, y1 - y0));
            }
            prevX = cx; prevY = cy;
        }

        // Band nodes (draggable handles).
        const auto& tree = processor_.bandTree;
        for (int i = 0; i < tree.getNumChildren(); ++i) {
            const auto b = tree.getChild (i);
            if (!(bool)b.getProperty ("enabled", false)) continue;
            const double f = b.getProperty ("freq", 1000.0);
            const double g = b.getProperty ("gain", 0.0);
            const float nx = px + pw * (float)(std::log10 (f / 20.0) / std::log10 (20000.0 / 20.0));
            const float ny = py + ph * 0.5f - ph * 0.5f * (float)(g / range);
            canvas.setColor (i == selected_ ? C::accentSoft : C::accent);
            canvas.circle (nx - 5.0f, ny - 5.0f, 10.0f);
            canvas.setColor (C::text);
            canvas.circle (nx - 2.5f, ny - 2.5f, 5.0f);
        }

        canvas.setColor (C::hairline);
        canvas.rectangleBorder (px, py, pw, ph, 1.0f);
    }

    void mouseDown (const visage::MouseEvent& e) override
    {
        if (!e.isLeftButtonCurrentlyDown()) return;
        selected_ = hitTest (e.position.x, e.position.y);
        if (selected_ >= 0) {
            dragging_ = true;
            lastDrag_ = e.position;
        }
        redraw();
    }

    void mouseDrag (const visage::MouseEvent& e) override
    {
        if (!dragging_ || selected_ < 0) return;
        const float dx = e.position.x - lastDrag_.x;
        const float dy = e.position.y - lastDrag_.y;
        lastDrag_ = e.position;

        auto b = processor_.bandTree.getChild (selected_);
        const float pw = width() - 44.0f;
        const double range = { 3.0, 6.0, 12.0, 30.0 }[(int)*processor_.parameters.getRawParameterValue ("eq_scale")];

        double f = b.getProperty ("freq", 1000.0);
        f = std::max (20.0, std::min (20000.0, f * std::pow (10.0, dx / pw * 3.0)));
        b.setProperty ("freq", f, nullptr);

        double g = b.getProperty ("gain", 0.0);
        const float ph = height() - 30.0f;
        g = std::max (-30.0, std::min (30.0, g - dy / ph * 2.0 * range));
        b.setProperty ("gain", g, nullptr);

        processor_.markBandsDirty();
        redraw();
    }

    void mouseUp (const visage::MouseEvent&) override { dragging_ = false; }

private:
    int hitTest (float x, float y)
    {
        const float px = 34.0f, py = 8.0f;
        const float pw = width() - px - 10.0f;
        const float ph = height() - py - 22.0f;
        const double range = { 3.0, 6.0, 12.0, 30.0 }[(int)*processor_.parameters.getRawParameterValue ("eq_scale")];

        int best = -1; float bestD = 12.0f * 12.0f;
        const auto& tree = processor_.bandTree;
        for (int i = 0; i < tree.getNumChildren(); ++i) {
            const auto b = tree.getChild (i);
            if (!(bool)b.getProperty ("enabled", false)) continue;
            const double f = b.getProperty ("freq", 1000.0);
            const double g = b.getProperty ("gain", 0.0);
            const float nx = px + pw * (float)(std::log10 (f / 20.0) / std::log10 (20000.0 / 20.0));
            const float ny = py + ph * 0.5f - ph * 0.5f * (float)(g / range);
            const float dx = x - nx, dy = y - ny;
            const float d = dx * dx + dy * dy;
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    FlushAudioProcessor& processor_;
    visage::Font labelFont_;
    int selected_ = -1;
    bool dragging_ = false;
    visage::Point lastDrag_;
};

// ============================================================ Preset bar =====
// Compact horizontal toolbar (Pro-Q style) directly under the header: prev/next
// arrows, the current preset name, and A/B compare. Replaces a full browser list
// so the input column stays clean for the trim knob + meter.
class FlushPresetBar : public visage::Frame
{
public:
    explicit FlushPresetBar (FlushAudioProcessor& p) : processor_ (p) {}

    void init() override
    {
        const float dpi = std::max (1.0f, dpiScale());
        label_font_ = makeFont (9.0f, dpi);
        value_font_ = makeFont (12.0f, dpi);
    }

    void draw (visage::Canvas& canvas) override
    {
        canvas.setColor (C::surface);
        canvas.fill (0, 0, width(), height());
        canvas.setColor (C::hairline);
        canvas.fill (0, height() - 1.0f, width(), 1.0f);

        drawNav (canvas, 6.0f, "<");
        drawNav (canvas, 34.0f, ">");

        canvas.setColor (C::label);
        canvas.text ("PRESET", label_font_, visage::Font::kTopLeft, 66.0f, 4.0f, 60.0f, 12.0f);

        const auto names = processor_.getFactoryPresetNames();
        const juce::String name = (current_ >= 0 && current_ < names.size())
            ? names[current_] : juce::String ("Init");
        canvas.setColor (C::text);
        canvas.text (name.toRawUTF8(), value_font_, visage::Font::kTopLeft,
                     66.0f, 15.0f, std::min (220.0f, width() - 300.0f), 15.0f);

        const float abX = width() - 110.0f;
        drawAb (canvas, abX, 5.0f, "A", abSlot_ == 0);
        drawAb (canvas, abX + 54.0f, 5.0f, "B", abSlot_ == 1);
    }

    void mouseDown (const visage::MouseEvent& e) override
    {
        if (!e.isLeftButtonCurrentlyDown()) return;
        const float x = e.position.x, y = e.position.y;
        if (y < 2.0f || y > height() - 2.0f) return;

        if (x < 30.0f) { prev(); }
        else if (x < 60.0f) { next(); }
        else if (x >= width() - 110.0f && x < width() - 56.0f) {
            if (abSlot_ == 0) processor_.recallAbSlot (0);
            else { processor_.storeAbSlot (0); abSlot_ = 0; }
        }
        else if (x >= width() - 56.0f) {
            if (abSlot_ == 1) processor_.recallAbSlot (1);
            else { processor_.storeAbSlot (1); abSlot_ = 1; }
        }
        redraw();
    }

private:
    void prev()
    {
        const auto names = processor_.getFactoryPresetNames();
        if (names.isEmpty()) return;
        current_ = (current_ <= 0) ? names.size() - 1 : current_ - 1;
        processor_.loadFactoryPreset (current_);
    }

    void next()
    {
        const auto names = processor_.getFactoryPresetNames();
        if (names.isEmpty()) return;
        current_ = (current_ + 1) % names.size();
        processor_.loadFactoryPreset (current_);
    }

    void drawNav (visage::Canvas& canvas, float x, const char* glyph)
    {
        canvas.setColor (C::surface2);
        canvas.roundedRectangle (x, 5.0f, 24.0f, 16.0f, 3.0f);
        canvas.setColor (C::text);
        canvas.text (glyph, value_font_, visage::Font::kCenter, x, 6.0f, 24.0f, 14.0f);
    }

    void drawAb (visage::Canvas& canvas, float x, float y, const char* label, bool active)
    {
        canvas.setColor (active ? C::accent : C::surface2);
        canvas.roundedRectangle (x, y, 46.0f, 16.0f, 3.0f);
        canvas.setColor (active ? C::bg0 : C::textDim);
        canvas.text (label, value_font_, visage::Font::kCenter, x, y + 1.0f, 46.0f, 14.0f);
    }

    FlushAudioProcessor& processor_;
    visage::Font label_font_, value_font_;
    int current_ = -1;
    int abSlot_ = -1;
};

// ============================================================ On/off toggle ===
class FlushToggle : public visage::Frame
{
public:
    FlushToggle (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& label)
        : param_ (apvts.getParameter (id)), label_ (label) {}

    void init() override
    {
        const float dpi = std::max (1.0f, dpiScale());
        label_font_ = makeFont (8.0f, dpi);
    }

    void draw (visage::Canvas& canvas) override
    {
        const bool on = param_ && param_->getValue() > 0.5f;
        canvas.setColor (C::label);
        canvas.text (label_.toRawUTF8(), label_font_, visage::Font::kCenter, 0.0f, 2.0f, width(), 10.0f);

        const float w = 22.0f, h = 12.0f;
        const float x = (width() - w) * 0.5f;
        const float y = height() - 17.0f;
        canvas.setColor (on ? C::accent : C::surface);
        canvas.roundedRectangle (x, y, w, h, 6.0f);
        const float kx = on ? x + w - h * 0.5f : x + h * 0.5f;
        canvas.setColor (C::text);
        canvas.circle (kx - 4.0f, y + 2.0f, 8.0f);
    }

    void mouseDown (const visage::MouseEvent& e) override
    {
        if (!param_ || !e.isLeftButtonCurrentlyDown()) return;
        if (auto* b = dynamic_cast<juce::AudioParameterBool*> (param_))
            b->setValueNotifyingHost (b->get() ? 0.0f : 1.0f);
        redraw();
    }

private:
    juce::AudioProcessorParameter* param_ = nullptr;
    juce::String label_;
    visage::Font label_font_;
};

// ============================================================ Settings gear ===
class FlushSettingsButton : public visage::Frame
{
public:
    explicit FlushSettingsButton (std::function<void()> onToggle) : onToggle_ (std::move (onToggle)) {}

    // Drawn gear (8 teeth + body + bore) — avoids depending on a gear glyph in
    // the embedded Lato font, which may not exist on every platform.
    void draw (visage::Canvas& canvas) override
    {
        canvas.setColor (C::surface);
        canvas.roundedRectangle (1.0f, 1.0f, width() - 2.0f, height() - 2.0f, 4.0f);

        const float cx = width() * 0.5f, cy = height() * 0.5f;
        const float r  = std::min (width(), height()) * 0.15f;
        const float tooth = r * 0.9f;

        canvas.setColor (C::textDim);
        for (int i = 0; i < 8; ++i) {
            const double a = i * 2.0 * kPi / 8.0;
            const float tx = (float)(cx + std::cos (a) * r * 1.9f);
            const float ty = (float)(cy + std::sin (a) * r * 1.9f);
            canvas.rectangle (tx - tooth * 0.35f, ty - tooth * 0.35f, tooth * 0.7f, tooth * 0.7f);
        }
        canvas.setColor (C::text);
        canvas.circle (cx - r, cy - r, r * 2.0f);
        canvas.setColor (C::surface);
        canvas.circle (cx - r * 0.45f, cy - r * 0.45f, r * 0.9f);
    }

    void mouseDown (const visage::MouseEvent& e) override
    {
        if (e.isLeftButtonCurrentlyDown() && onToggle_) onToggle_();
    }

private:
    std::function<void()> onToggle_;
};

// ============================================================ Settings modal ==
class FlushSettingsModal : public visage::Frame
{
public:
    FlushSettingsModal (FlushAudioProcessor& p, std::function<void()> onClose)
        : processor_ (p), onClose_ (std::move (onClose)) {}

    void init() override
    {
        const float dpi = std::max (1.0f, dpiScale());
        title_font_ = makeFont (14.0f, dpi);
        label_font_ = makeFont (10.0f, dpi);
        value_font_ = makeFont (12.0f, dpi);
    }

    void draw (visage::Canvas& canvas) override
    {
        if (width() < 2.0f) return;                       // hidden

        // Dim the whole surface behind the modal (dark scrim).
        canvas.setColor (0xcc000000);
        canvas.fill (0, 0, width(), height());

        // Panel.
        const float pw = std::min (width(), 420.0f);
        const float ph = std::min (height(), 320.0f);
        const float px = (width() - pw) * 0.5f;
        const float py = (height() - ph) * 0.5f;

        canvas.setColor (C::surface2);
        canvas.roundedRectangle (px, py, pw, ph, 8.0f);
        canvas.setColor (C::hairline);
        canvas.roundedRectangleBorder (px, py, pw, ph, 8.0f, 1.0f);

        canvas.setColor (C::text);
        canvas.text ("SETTINGS", title_font_, visage::Font::kTopLeft, px + 20.0f, py + 16.0f, 200.0f, 20.0f);

        // UI Scale slider.
        const float sx = px + 24.0f, sy = py + 56.0f, sw = pw - 48.0f;
        canvas.setColor (C::label);
        canvas.text ("UI SCALE", label_font_, visage::Font::kTopLeft, sx, sy, 120.0f, 14.0f);
        const float scale = *processor_.parameters.getRawParameterValue ("ui_scale");
        canvas.setColor (C::text);
        canvas.text ((juce::String (scale, 2) + " x").toRawUTF8(), value_font_,
                     visage::Font::kTopRight, sx, sy, 120.0f, 16.0f);

        canvas.setColor (C::stroke);
        canvas.roundedRectangle (sx, sy + 20.0f, sw, 6.0f, 3.0f);
        const float knobX = sx + (scale - 0.5f) / 1.5f * sw;
        canvas.setColor (C::accent);
        canvas.circle (knobX - 7.0f, sy + 16.0f, 14.0f);

        // UI Refresh choice.
        const float ry = sy + 56.0f;
        canvas.setColor (C::label);
        canvas.text ("UI REFRESH", label_font_, visage::Font::kTopLeft, sx, ry, 120.0f, 14.0f);
        const int fps = (int)*processor_.parameters.getRawParameterValue ("fps_mode");
        canvas.setColor (C::accent);
        canvas.text (juce::StringArray { "60", "120", "Uncapped" }[fps].toRawUTF8(),
                     value_font_, visage::Font::kTopRight, sx, ry, 120.0f, 16.0f);

        // Accent swatches.
        const float ay = ry + 40.0f;
        canvas.setColor (C::label);
        canvas.text ("ACCENT", label_font_, visage::Font::kTopLeft, sx, ay, 120.0f, 14.0f);
        const unsigned int accents[4] = { 0xff3ea6ff, 0xff3ee6e0, 0xffff9e5c, 0xffb18cff };
        for (int i = 0; i < 4; ++i) {
            canvas.setColor (accents[i]);
            canvas.circle (sx + 20.0f + i * 30.0f, ay + 24.0f, 20.0f);
            const int cur = (int)*processor_.parameters.getRawParameterValue ("theme_accent");
            if (i == cur) { canvas.setColor (C::text); canvas.circle (sx + 20.0f + i * 30.0f - 3.0f, ay + 21.0f, 6.0f); }
        }
    }

    void mouseDown (const visage::MouseEvent& e) override
    {
        if (!e.isLeftButtonCurrentlyDown()) return;
        const float pw = std::min (width(), 420.0f);
        const float ph = std::min (height(), 320.0f);
        const float px = (width() - pw) * 0.5f;
        const float py = (height() - ph) * 0.5f;
        const float sx = px + 24.0f, sy = py + 56.0f, sw = pw - 48.0f;

        // Scale slider.
        if (e.position.y >= sy + 16.0f && e.position.y <= sy + 30.0f) {
            dragging_ = true;
            dragScale (e);
            return;
        }

        // Refresh row.
        const float ry = sy + 56.0f;
        if (e.position.y >= ry && e.position.y <= ry + 20.0f) {
            auto* c = dynamic_cast<juce::AudioParameterChoice*> (processor_.parameters.getParameter ("fps_mode"));
            if (c) c->setValueNotifyingHost ((float)((c->getIndex() + 1) % 3) / 2.0f);
            redraw();
            return;
        }

        // Accent swatches.
        const float ay = ry + 40.0f;
        if (e.position.y >= ay + 18.0f && e.position.y <= ay + 38.0f) {
            const int i = (int)((e.position.x - (sx + 10.0f)) / 30.0f);
            if (i >= 0 && i < 4) {
                auto* c = dynamic_cast<juce::AudioParameterChoice*> (processor_.parameters.getParameter ("theme_accent"));
                if (c) c->setValueNotifyingHost ((float)i / 3.0f);
            }
            redraw();
        }

        // Click outside the panel (scrim) closes the modal.
        if (e.position.x < px || e.position.x > px + pw || e.position.y < py || e.position.y > py + ph)
            if (onClose_) onClose_();
    }

    void mouseDrag (const visage::MouseEvent& e) override
    {
        if (dragging_) dragScale (e);
    }

    void mouseUp (const visage::MouseEvent&) override { dragging_ = false; }

private:
    void dragScale (const visage::MouseEvent& e)
    {
        const float pw = std::min (width(), 420.0f);
        const float ph = std::min (height(), 320.0f);
        const float px = (width() - pw) * 0.5f;
        const float py = (height() - ph) * 0.5f;
        const float sx = px + 24.0f, sw = pw - 48.0f;
        const float t = clamp ((e.position.x - sx) / std::max (1.0f, sw), 0.0f, 1.0f);
        auto* p = dynamic_cast<juce::AudioParameterFloat*> (processor_.parameters.getParameter ("ui_scale"));
        if (p) p->setValueNotifyingHost (0.5f + 1.5f * t);
        redraw();
    }

    FlushAudioProcessor& processor_;
    std::function<void()> onClose_;
    visage::Font title_font_, label_font_, value_font_;
    bool dragging_ = false;
};

// ============================================================ Main view ======
class FlushMainView : public visage::Frame
{
public:
    explicit FlushMainView (FlushAudioProcessor& p) : processor (p) {}

    void init() override
    {
        const float dpi = std::max (1.0f, dpiScale());
        title_font_ = makeFont (18.0f, dpi);
        label_font_ = makeFont (10.0f, dpi);
        value_font_ = makeFont (13.0f, dpi);

        // 0: preset bar (toolbar under the header, full width).
        addChild (std::make_unique<FlushPresetBar> (processor));

        // 1-2: input section.
        addChild (std::make_unique<FlushKnob> (processor.parameters, "input_gain", "INPUT TRIM"));
        addChild (std::make_unique<FlushMeter> (processor.parameters, processor.meterInRms, processor.meterInPeak));

        // 3: EQ display (analyzer + draggable nodes).
        addChild (std::make_unique<FlushEqDisplay> (processor));

        // 4-7: compressor section.
        addChild (std::make_unique<FlushKnob> (processor.parameters, "comp_threshold", "THRESH"));
        addChild (std::make_unique<FlushKnob> (processor.parameters, "comp_ratio", "RATIO"));
        addChild (std::make_unique<FlushKnob> (processor.parameters, "comp_attack", "ATTACK"));
        addChild (std::make_unique<FlushKnob> (processor.parameters, "comp_release", "RELEASE"));

        // 8-11: Flush Match + output section.
        addChild (std::make_unique<FlushChoiceButton> (processor.parameters, "flush_mode", "FLUSH MATCH"));
        addChild (std::make_unique<FlushKnob> (processor.parameters, "match_target", "TARGET"));
        addChild (std::make_unique<FlushKnob> (processor.parameters, "output_gain", "OUTPUT TRIM"));
        addChild (std::make_unique<FlushMeter> (processor.parameters, processor.meterOutRms, processor.meterOutPeak));

        // 12-16: header controls (phase, meter, analyzer, eq, gear).
        addChild (std::make_unique<FlushChoiceButton> (processor.parameters, "eq_phase_mode", "PHASE"));
        addChild (std::make_unique<FlushChoiceButton> (processor.parameters, "meter_mode", "METER"));
        addChild (std::make_unique<FlushToggle> (processor.parameters, "analyzer_on", "ANALYZER"));
        addChild (std::make_unique<FlushToggle> (processor.parameters, "eq_enabled", "EQ"));

        addChild (std::make_unique<FlushSettingsButton> ([this] { setModalVisible (!modalVisible_); }));

        // 17: compressor gain-reduction meter.
        addChild (std::make_unique<FlushGrMeter> (processor.meterGrDb));

        // 18: settings modal (overlay, drawn last = on top).
        modal_ = new FlushSettingsModal (processor, [this] { setModalVisible (false); });
        addChild (std::unique_ptr<FlushSettingsModal> (modal_));
    }

    void setModalVisible (bool visible)
    {
        modalVisible_ = visible;
        if (modal_)
            modal_->setBounds (visible ? 0.0f : 0.0f, visible ? 0.0f : 0.0f,
                               visible ? width() : 0.0f, visible ? height() : 0.0f);
        redraw();
    }

    void resized() override
    {
        const float w = width(), h = height();
        const float header  = 46.0f;
        const float preseth = 28.0f;
        const float bodyY   = header + preseth;
        const float bodyH   = h - bodyY;

        // Body regions (15/42/18/25).
        const float inW   = w * 0.15f;
        const float eqW   = w * 0.42f;
        const float compW = w * 0.18f;
        const float outW  = w - inW - eqW - compW;

        auto child = [&](int i) { return children()[i]; };

        // 0: preset toolbar, full width under the header.
        child (0)->setBounds (0.0f, header, w, preseth);

        // 1-2: input knob + meter.
        child (1)->setBounds (inW * 0.5f - 26.0f, bodyY + 8.0f, 52.0f, 78.0f);
        child (2)->setBounds (inW * 0.5f - 16.0f, bodyY + 96.0f, 32.0f, bodyH - 120.0f);

        // 3: EQ display.
        child (3)->setBounds (inW, bodyY, eqW, bodyH);

        // 4-7: compressor knobs (stacked).
        const float compX = inW + eqW;
        for (int i = 0; i < 4; ++i)
            child (4 + i)->setBounds (compX + compW * 0.5f - 22.0f, bodyY + 12.0f + i * 92.0f, 44.0f, 66.0f);

        // 8-11: Flush Match + output.
        const float outX = compX + compW;
        child (8)->setBounds  (outX + 8.0f, bodyY + 8.0f, outW - 16.0f, 34.0f);              // mode
        child (9)->setBounds  (outX + outW * 0.5f - 22.0f, bodyY + 56.0f, 44.0f, 66.0f);     // target
        child (10)->setBounds (outX + outW * 0.5f - 26.0f, bodyY + 132.0f, 52.0f, 78.0f);    // output trim
        child (11)->setBounds (outX + outW - 40.0f, bodyY + 220.0f, 32.0f, bodyH - 244.0f);  // out meter

        // 12-16: header controls (right side): phase, meter, analyzer, eq, gear.
        child (12)->setBounds (w - 364.0f, 6.0f, 96.0f, 36.0f);
        child (13)->setBounds (w - 262.0f, 6.0f, 96.0f, 36.0f);
        child (14)->setBounds (w - 160.0f, 6.0f, 52.0f, 36.0f);
        child (15)->setBounds (w - 102.0f, 6.0f, 52.0f, 36.0f);
        child (16)->setBounds (w - 44.0f, 6.0f, 40.0f, 36.0f);

        // 17: compressor GR meter (right edge of the compressor column).
        child (17)->setBounds (compX + compW - 26.0f, bodyY + 8.0f, 18.0f, bodyH - 40.0f);

        // 18: settings modal overlay (full window when visible).
        if (modal_ && modalVisible_)
            modal_->setBounds (0.0f, 0.0f, w, h);
    }

    void draw (visage::Canvas& canvas) override
    {
        // Vertical gradient base (near-black panel).
        canvas.setColor (C::bg1);
        canvas.fill (0, 0, width(), height());
        canvas.setColor (C::bg0);
        canvas.fill (0, (int)(height() * 0.35f), width(), (int)(height() * 0.65f));

        // Header + branding.
        canvas.setColor (C::stroke);
        canvas.fill (0, 0, width(), 46.0f);
        canvas.setColor (C::hairline);
        canvas.fill (0, 46.0f, width(), 1.0f);

        canvas.setColor (C::text);
        canvas.text ("FLUSH", title_font_, visage::Font::kTopLeft, 20.0f, 8.0f, 140.0f, 22.0f);
        canvas.setColor (C::label);
        canvas.text ("GAIN-STAGED DYNAMIC EQ", label_font_, visage::Font::kTopLeft, 20.0f, 32.0f, 240.0f, 12.0f);

        // Live Flush delta (the hero readout), centered in the header.
        const float delta = processor.meterFlushDb.load();
        canvas.setColor (delta >= 0.0f ? C::green : C::yellow);
        canvas.text ((juce::String ("FLUSH ") + juce::String (delta, 1) + " dB").toRawUTF8(),
                     value_font_, visage::Font::kCenter, width() * 0.5f - 120.0f, 13.0f, 240.0f, 20.0f);

        // Latency readout (just left of the header controls).
        const float latMs = processor.meterLatencyMs.load();
        canvas.setColor (C::textDim);
        canvas.text ((juce::String ("LATENCY ") + juce::String (latMs, 1) + " MS").toRawUTF8(),
                     label_font_, visage::Font::kTopRight, width() - 462.0f, 18.0f, 90.0f, 12.0f);
    }

private:
    FlushAudioProcessor& processor;
    visage::Font title_font_, label_font_, value_font_;
    FlushSettingsModal* modal_ = nullptr;
    bool modalVisible_ = false;
};
