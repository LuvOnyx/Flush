#pragma once

// Flush — Visage UI (native C++, GPU-rendered) — FabFilter Pro-Q4 quality bar.
//
// This is the "not web appy" front end: real vertical gradients, per-band colour
// nodes with hover + glow, draggable nodes, Q editing via mouse wheel, a
// selected-band editor strip with value scrubbing, value bubbles while dragging,
// a themable accent colour, and a 120 Hz+ render loop (see PluginEditor).
//
// NOTE: written against the verified Visage API (Frame, Canvas, Brush::vertical
// gradients, PopupMenu, MouseEvent, KeyEvent). Not compile-verified here — the
// first build is the job of the local agent (see HANDOFF.md).

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
        constexpr unsigned int bg0      = 0xff101216;
        constexpr unsigned int bg1      = 0xff1b1e24;
        constexpr unsigned int surface  = 0xff23272f;
        constexpr unsigned int surface2 = 0xff2a3038;
        constexpr unsigned int stroke   = 0xff0b0d10;
        constexpr unsigned int hairline = 0xff343b45;
        constexpr unsigned int text     = 0xffd4dbe3;
        constexpr unsigned int textDim  = 0xff9099a5;
        constexpr unsigned int label    = 0xff6c7480;
        constexpr unsigned int green    = 0xff3ddc84;
        constexpr unsigned int yellow   = 0xffffd166;
        constexpr unsigned int red      = 0xffff5c5c;
    }

    inline float clamp (float v, float lo, float hi) { return std::max (lo, std::min (hi, v)); }

    inline visage::Font makeFont (float size, float dpi)
    {
        const auto* d = reinterpret_cast<const unsigned char*> (flush_BinaryData::LatoRegular_ttf);
        return visage::Font (size, d, flush_BinaryData::LatoRegular_ttfSize, dpi);
    }

    // HSV -> 0xAARRGGBB (h in turns, s/v/a in 0..1). Used for per-band colours.
    inline unsigned int hsvToArgb (float h, float s, float v, float a)
    {
        h = h - std::floor (h);
        const float c = v * s;
        const float x = c * (1.0f - std::fabs (std::fmod (h * 6.0f, 2.0f) - 1.0f));
        const float m = v - c;
        float r = 0, g = 0, b = 0;
        if      (h < 1.0f/6.0f) { r = c; g = x; }
        else if (h < 2.0f/6.0f) { r = x; g = c; }
        else if (h < 3.0f/6.0f) { g = c; b = x; }
        else if (h < 4.0f/6.0f) { g = x; b = c; }
        else if (h < 5.0f/6.0f) { r = x; b = c; }
        else                    { r = c; b = x; }
        auto u = [](float f){ return (unsigned int)(clamp (f + m, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return ((unsigned int)(a * 255.0f + 0.5f) << 24) | (u(r) << 16) | (u(g) << 8) | u(b);
    }

    inline unsigned int bandColorFor (int i)
    {
        // Walk a hue wheel starting in the blue-cyan region (Pro-Q vibes).
        return hsvToArgb (0.55f + 0.14f * (i % 12), 0.52f, 0.94f, 1.0f);
    }

    inline int accentIndex (juce::AudioProcessorValueTreeState& a)
    {
        return juce::jlimit (0, 3, (int)*a.getRawParameterValue ("theme_accent"));
    }

    inline unsigned int accentFor (juce::AudioProcessorValueTreeState& a)
    {
        static const unsigned int acc[4] = { 0xff3ea6ff, 0xff3ee6e0, 0xffff9e5c, 0xffb18cff };
        return acc[accentIndex (a)];
    }

    inline unsigned int accentSoftFor (juce::AudioProcessorValueTreeState& a)
    {
        return visage::Color (accentFor (a))
            .interpolateWith (visage::Color (0xffffffff), 0.45f).toARGB();
    }

    // A translucent glow colour derived from the accent (for halos).
    inline unsigned int accentGlowFor (juce::AudioProcessorValueTreeState& a, float alpha)
    {
        return visage::Color (accentFor (a)).withAlpha (alpha).toARGB();
    }
}

// ============================================================ Glassy knob ====
class FlushKnob : public visage::Frame
{
public:
    FlushKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& label)
        : apvts_ (apvts), param_ (apvts.getParameter (id)), label_ (label) {}

    void init() override
    {
        const float dpi = std::max (1.0f, dpiScale());
        labelFont_ = makeFont (9.0f, dpi);
        valueFont_ = makeFont (12.0f, dpi);
    }

    void draw (visage::Canvas& canvas) override
    {
        if (width() < 4.0f || height() < 4.0f) return;
        const float cx = width() * 0.5f;
        const float cy = width() * 0.5f;
        const float r  = width() * 0.40f;
        const float value = param_ ? param_->getValue() : 0.0f;
        const unsigned int accent = accentFor (apvts_);
        const unsigned int accentSoft = accentSoftFor (apvts_);

        // Hover glow halo.
        if (hovered_)
            canvas.setColor (visage::Brush::vertical (
                visage::Color (accentGlowFor (apvts_, 0.20f)),
                visage::Color (0x00000000)));

        // Machined ring + body with a real vertical gradient (not flat fill).
        canvas.setColor (C::hairline);
        canvas.circle (cx - r - 2.0f, cy - r - 2.0f, (r + 2.0f) * 2.0f);
        canvas.setColor (visage::Brush::vertical (
            visage::Color (0xff4a505c), visage::Color (0xff262b33)));
        canvas.circle (cx - r, cy - r, r * 2.0f);
        canvas.setColor (C::stroke);
        canvas.circle (cx - r + 1.5f, cy - r + 1.5f, (r - 3.0f) * 2.0f);

        // Glass catch-light.
        canvas.setColor (0x2e565d6b);
        canvas.circle (cx - r * 0.55f, cy - r * 0.72f, r * 0.55f);

        // Inactive track (270° sweep, gap at bottom).
        canvas.setColor (C::surface2);
        canvas.arc (cx - r, cy - r, r * 2.0f, 3.0f, (float)kPi, -(float)(kPi * 1.5f), true);

        // Value arc.
        canvas.setColor (hovered_ ? accentSoft : accent);
        canvas.arc (cx - r, cy - r, r * 2.0f, 3.0f, (float)kPi, -(float)(kPi * 1.5 * value), true);

        // Pointer.
        const double ang = (-135.0 + 270.0 * value) * (kPi / 180.0);
        const float px = (float)(cx + r * 0.72 * std::sin (ang));
        const float py = (float)(cy - r * 0.72 * std::cos (ang));
        canvas.setColor (accentSoft);
        canvas.circle (px - 3.0f, py - 3.0f, 6.0f);

        // Label + value.
        canvas.setColor (C::label);
        canvas.text (label_.toRawUTF8(), labelFont_, visage::Font::kCenter, 0.0f, height() - 34.0f, width(), 12.0f);
        canvas.setColor (C::text);
        canvas.text (param_ ? param_->getText (value, 0).toRawUTF8() : "",
                     valueFont_, visage::Font::kCenter, 0.0f, height() - 22.0f, width(), 16.0f);

        // Value bubble while dragging (FabFilter-style drag readout).
        if (dragging_ && param_) {
            const juce::String txt = param_->getText (param_->getValue(), 0);
            const float bw = 72.0f;
            canvas.setColor (0xee2a3038);
            canvas.roundedRectangle (cx - bw * 0.5f, cy - r - 30.0f, bw, 20.0f, 5.0f);
            canvas.setColor (C::hairline);
            canvas.roundedRectangleBorder (cx - bw * 0.5f, cy - r - 30.0f, bw, 20.0f, 5.0f, 1.0f);
            canvas.setColor (C::text);
            canvas.text (txt.toRawUTF8(), valueFont_, visage::Font::kCenter,
                         cx - bw * 0.5f, cy - r - 29.0f, bw, 18.0f);
        }
    }

    void mouseEnter (const visage::MouseEvent&) override { hovered_ = true; redraw(); }
    void mouseExit  (const visage::MouseEvent&) override { hovered_ = false; dragging_ = false; redraw(); }

    void mouseDown (const visage::MouseEvent& e) override
    {
        if (!param_ || !e.isLeftButtonCurrentlyDown()) return;
        dragging_ = true;
        dragStart_ = e.position.y;
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastClick_ < 400)                       // double-click resets
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

    bool mouseWheel (const visage::MouseEvent& e) override
    {
        if (!param_) return false;
        const double sens = e.isShiftDown() ? 0.002 : 0.006;
        param_->setValueNotifyingHost ((float)juce::jlimit (0.0, 1.0,
            param_->getValue() - e.wheel_delta_y * sens));
        redraw();
        return true;
    }

private:
    juce::AudioProcessorValueTreeState& apvts_;
    juce::AudioProcessorParameter* param_ = nullptr;
    juce::String label_;
    visage::Font labelFont_, valueFont_;
    bool dragging_ = false, hovered_ = false;
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
        if (width() < 12.0f || height() < 30.0f) return;
        const float x = 4.0f, w = width() - 8.0f;
        const float h = height() - 24.0f;
        const float y0 = 2.0f;

        // Gradient well.
        canvas.setColor (visage::Brush::vertical (
            visage::Color (0xff181b21), visage::Color (C::surface)));
        canvas.rectangle (x, y0, w, h);

        const double lvl = clamp ((float)((db + 60.0) / 60.0), 0.0f, 1.0f);
        const float filled = h * (float)lvl;

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

        const double pk = clamp ((float)((peak_.load() + 60.0) / 60.0), 0.0f, 1.0f);
        canvas.setColor (0xfff2f5f8);
        canvas.rectangle (x, y0 + h - h * (float)pk, w, 2.0f);

        canvas.setColor (C::hairline);
        canvas.rectangleBorder (x, y0, w, h, 1.0f);
        canvas.setColor (C::label);
        for (double tick : { -60.0, -30.0, -18.0, -6.0, 0.0 }) {
            const float ty = y0 + h - h * (float)((tick + 60.0) / 60.0);
            canvas.text (juce::String ((int)tick).toRawUTF8(), labelFont_, visage::Font::kRight,
                         0.0f, ty - 5.0f, x - 2.0f, 10.0f);
        }

        canvas.setColor (C::text);
        canvas.text (juce::String (db, 1).toRawUTF8(), labelFont_, visage::Font::kCenter,
                     0.0f, height() - 12.0f, width(), 10.0f);
    }

    void drawVU (visage::Canvas& canvas, double db)
    {
        if (width() < 10.0f || height() < 30.0f) return;
        const float cx = width() * 0.5f;
        const float cy = height() - 40.0f;
        const float r  = std::min (width(), height() - 44.0f) * 0.46f;

        canvas.setColor (C::surface);
        canvas.arc (cx - r, cy - r, r * 2.0f, 5.0f, (float)(kPi * 0.75), (float)(kPi * 1.5), false);

        const double t = clamp ((float)((db + 60.0) / 65.0), 0.0f, 1.0f);
        const double ang = (0.25 - 0.5 * t) * kPi;
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
        if (width() < 12.0f || height() < 30.0f) return;
        const float x = 4.0f, w = width() - 8.0f;
        const float y0 = 2.0f, h = height() - 24.0f;

        canvas.setColor (visage::Brush::vertical (
            visage::Color (0xff181b21), visage::Color (C::surface)));
        canvas.rectangle (x, y0, w, h);

        const float gr = clamp (gr_.load() / 24.0f, 0.0f, 1.0f);
        const float filled = h * gr;
        if (filled > 0.5f) {
            canvas.setColor (visage::Brush::vertical (
                visage::Color (0xff8fcbff), visage::Color (0xff3ea6ff)));
            canvas.rectangle (x, y0, w, filled);
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
        canvas.setColor (hovered_ ? C::surface2 : C::surface);
        canvas.roundedRectangle (1.0f, 10.0f, width() - 2.0f, height() - 12.0f, 4.0f);
        canvas.setColor (C::hairline);
        canvas.roundedRectangleBorder (1.0f, 10.0f, width() - 2.0f, height() - 12.0f, 4.0f, 1.0f);

        canvas.setColor (C::label);
        canvas.text (label_.toRawUTF8(), labelFont_, visage::Font::kCenter, 0.0f, 1.0f, width(), 10.0f);
        canvas.setColor (accentFor (apvts_));
        canvas.text (param_ ? param_->getCurrentValueAsText().toRawUTF8() : "",
                     valueFont_, visage::Font::kCenter, 0.0f, height() - 16.0f, width(), 14.0f);
    }

    void mouseEnter (const visage::MouseEvent&) override { hovered_ = true; redraw(); }
    void mouseExit  (const visage::MouseEvent&) override { hovered_ = false; redraw(); }

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
    bool hovered_ = false;
};

// ============================================== On/off toggle ================
class FlushToggle : public visage::Frame
{
public:
    FlushToggle (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& label)
        : apvts_ (apvts), param_ (apvts.getParameter (id)), label_ (label) {}

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
        canvas.setColor (on ? accentFor (apvts_) : C::surface);
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
    juce::AudioProcessorValueTreeState& apvts_;
    juce::AudioProcessorParameter* param_ = nullptr;
    juce::String label_;
    visage::Font label_font_;
};

// ============================================== EQ display (analyzer + nodes) ==
// Interactions (Pro-Q style):
//   * left-drag a node          -> frequency (horizontal) + gain (vertical)
//   * mouse wheel over a node   -> Q (bandwidth)
//   * left-click empty space    -> deselect
//   * DOUBLE-click empty space  -> ADD a bell band
//   * DOUBLE-click a node       -> reset gain
//   * RIGHT-click node/space    -> context menu (type / dynamic / solo / remove / add)
//   * selected-band strip       -> type cycle + freq/gain/Q value scrubbing
//   * keyboard                  -> Delete/Backspace remove, arrows nudge
class FlushEqDisplay : public visage::Frame
{
public:
    explicit FlushEqDisplay (FlushAudioProcessor& p) : processor_ (p) {}

    void init() override
    {
        const float dpi = std::max (1.0f, dpiScale());
        labelFont_ = makeFont (8.0f, dpi);
        smallFont_ = makeFont (10.0f, dpi);
        valueFont_ = makeFont (11.0f, dpi);
        cachedBands_ = processor_.getBandTreeCopy();
    }

    // Geometry: [plot] [freq labels] [selected-band strip]
    float plotX() const  { return 38.0f; }
    float plotY() const  { return 6.0f; }
    float plotW() const  { return width() - plotX() - 10.0f; }
    float plotH() const  { return height() - 6.0f - 18.0f - stripH(); }
    float stripH() const { return 28.0f; }
    float stripY() const { return height() - stripH(); }

    void draw (visage::Canvas& canvas) override
    {
        if (width() < 44.0f || height() < 30.0f)
            return;

        cachedBands_ = processor_.getBandTreeCopy();
        const float px = plotX(), py = plotY(), pw = plotW(), ph = plotH();
        const double range = displayRange();
        const unsigned int accent = accentFor (processor_.parameters);
        const unsigned int accentSoft = accentSoftFor (processor_.parameters);

        // Plot well (gradient).
        canvas.setColor (visage::Brush::vertical (
            visage::Color (0xff14161b), visage::Color (0xff1e2229)));
        canvas.rectangle (px, py, pw, ph);

        // Spectrum analyzer (quantised alpha bands = cheap gradient).
        if (*processor_.parameters.getRawParameterValue ("analyzer_on") > 0.5f) {
            const auto& mags = processor_.analyzerMagnitudes();
            const int N = (int)mags.size();
            if (N > 0) {
                const float colW = pw / N;
                const float rangeDb = { 60.0f, 90.0f, 120.0f }[(int)*processor_.parameters.getRawParameterValue ("analyzer_range")];
                for (int i = 0; i < N; ++i) {
                    const float lvl = clamp (mags[i] / rangeDb + 1.0f, 0.0f, 1.0f);
                    const float colH = ph * lvl;
                    if (colH < 0.5f) continue;
                    const float cx = px + i * colW, cw2 = std::max (1.0f, colW - 0.5f);
                    // Body (fades toward the top via 3 alpha bands).
                    canvas.setColor (visage::Color (accent).withAlpha (0.30f).toARGB());
                    canvas.rectangle (cx, py + ph - colH, cw2, colH);
                    canvas.setColor (visage::Color (accent).withAlpha (0.55f).toARGB());
                    canvas.rectangle (cx, py + ph - colH * 0.6f, cw2, colH * 0.6f);
                    // Peak hairline.
                    canvas.setColor (accentSoft);
                    canvas.rectangle (cx, py + ph - colH, cw2, 2.0f);
                }
            }
        }

        // dB grid + scale labels (left).
        canvas.setColor (C::stroke);
        for (double g : { -30.0, -20.0, -10.0, 0.0, 10.0, 20.0, 30.0 }) {
            if (std::fabs (g) > range) continue;
            const float gy = py + ph * 0.5f - ph * 0.5f * (float)(g / range);
            canvas.rectangle (px, gy, pw, 1.0f);
            canvas.setColor (C::label);
            canvas.text (juce::String ((int)g).toRawUTF8(), labelFont_, visage::Font::kRight,
                         0.0f, gy - 5.0f, px - 4.0f, 10.0f);
            canvas.setColor (C::stroke);
        }

        // Frequency grid (log) + labels.
        for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 }) {
            const float gx = px + pw * (float)(std::log10 (f / 20.0) / std::log10 (20000.0 / 20.0));
            canvas.setColor (C::stroke);
            canvas.rectangle (gx, py, 1.0f, ph);
            canvas.setColor (C::label);
            canvas.text ((juce::String ((int)f) + (f >= 1000 ? "k" : "")).toRawUTF8(), labelFont_,
                         visage::Font::kCenter, gx - 15.0f, py + ph + 2.0f, 30.0f, 12.0f);
        }

        // EQ curve (accent, slightly translucent).
        const int curveN = 160;
        float prevX = px, prevY = py + ph * 0.5f;
        for (int i = 0; i <= curveN; ++i) {
            const double f = 20.0 * std::pow (20000.0 / 20.0, (double)i / curveN);
            const double db = processor_.eqResponseDb (f);
            const float cx = px + pw * (float)((double)i / curveN);
            const float cy = py + ph * 0.5f - ph * 0.5f * (float)(db / range);
            if (i > 0) {
                canvas.setColor (0xffffffff);
                const float x0 = std::min (prevX, cx), x1 = std::max (prevX, cx);
                const float y0 = std::min (prevY, cy), y1 = std::max (prevY, cy);
                canvas.rectangle (x0, y0, std::max (1.0f, x1 - x0), std::max (1.0f, y1 - y0));
            }
            prevX = cx; prevY = cy;
        }

        // Band nodes: per-band colour, glow on hover/selection.
        const int count = juce::jmin ((int)cachedBands_.getNumChildren(), FlushAudioProcessor::kMaxBands);
        for (int i = 0; i < count; ++i) {
            const auto b = cachedBands_.getChild (i);
            if (!(bool)b.getProperty ("enabled", false)) continue;
            const double f = b.getProperty ("freq", 1000.0);
            const double g = b.getProperty ("gain", 0.0);
            const double q = b.getProperty ("q", 1.0);
            const bool dyn = (bool)b.getProperty ("dynamic", false);
            const float nx = px + pw * (float)(std::log10 (f / 20.0) / std::log10 (20000.0 / 20.0));
            const float ny = py + ph * 0.5f - ph * 0.5f * (float)(g / range);
            const unsigned int bc = bandColorFor (i);
            const bool active = (i == selected_ || i == hovered_);

            if (active) {  // glow halo
                canvas.setColor (visage::Color (bc).withAlpha (0.25f).toARGB());
                canvas.circle (nx - 9.0f, ny - 9.0f, 18.0f);
            }
            canvas.setColor (bc);
            canvas.circle (nx - 5.0f, ny - 5.0f, 10.0f);
            canvas.setColor (dyn ? C::yellow : 0xfff2f5f8);
            canvas.circle (nx - 2.5f, ny - 2.5f, 5.0f);
            (void)q;
        }

        canvas.setColor (C::hairline);
        canvas.rectangleBorder (px, py, pw, ph, 1.0f);

        drawAnalyzerToolbar (canvas, accent, accentSoft);
        drawSelectedStrip (canvas, accent, accentSoft);

        // Value bubble while dragging / scrubbing.
        if (dragging_ || scrubbing_ >= 0) {
            const float bw = 150.0f;
            canvas.setColor (0xee2a3038);
            canvas.roundedRectangle (bubblePos_.x - bw * 0.5f, bubblePos_.y - 34.0f, bw, 22.0f, 5.0f);
            canvas.setColor (C::hairline);
            canvas.roundedRectangleBorder (bubblePos_.x - bw * 0.5f, bubblePos_.y - 34.0f, bw, 22.0f, 5.0f, 1.0f);
            canvas.setColor (C::text);
            canvas.text (bubbleText_.toRawUTF8(), valueFont_, visage::Font::kCenter,
                         bubblePos_.x - bw * 0.5f, bubblePos_.y - 33.0f, bw, 20.0f);
        }
    }

    // Proportional strip layout so the selected-band controls never clip at
    // small UI scales (the EQ display can be as narrow as ~230 px at 0.5x).
    struct StripLayout {
        float pad, typeW, valW, tglW;
        float typeX, freqX, gainX, qX, dynX, soloX, y, h;
    };
    StripLayout stripLayout() const
    {
        StripLayout s;
        s.y = stripY(); s.h = stripH();
        s.pad = 6.0f;
        s.typeW = std::min (100.0f, width() * 0.24f);
        s.tglW = 40.0f;
        s.valW = std::max (30.0f, (width() - s.pad * 2.0f - s.typeW - s.tglW * 2.0f - 12.0f) / 3.0f);
        s.typeX = s.pad;
        s.freqX = s.typeX + s.typeW + 4.0f;
        s.gainX = s.freqX + s.valW;
        s.qX    = s.gainX + s.valW;
        s.dynX  = s.qX + s.valW + 8.0f;
        s.soloX = s.dynX + s.tglW;
        return s;
    }

    // Analyzer toolbar (top-right of the plot): Freeze toggle + In/Out source.
    // Matches Pro-Q's analyzer controls living right on the display.
    void drawAnalyzerToolbar (visage::Canvas& canvas, unsigned int accent, unsigned int)
    {
        const float pw = plotW();
        const float bx = plotX() + pw - 128.0f;
        const float by = plotY() + 4.0f;
        const float bw = 58.0f, bh = 16.0f;

        const bool frozen = *processor_.parameters.getRawParameterValue ("analyzer_freeze") > 0.5f;
        const int  source = (int)*processor_.parameters.getRawParameterValue ("analyzer_source");

        // Freeze.
        canvas.setColor (frozen ? accent : C::surface2);
        canvas.roundedRectangle (bx, by, bw, bh, 3.0f);
        canvas.setColor (frozen ? C::bg0 : C::textDim);
        canvas.text ("FREEZE", labelFont_, visage::Font::kCenter, bx, by + 2.0f, bw, bh - 4.0f);

        // In / Out.
        canvas.setColor (source == 1 ? accent : C::surface2);
        canvas.roundedRectangle (bx + bw + 6.0f, by, bw, bh, 3.0f);
        canvas.setColor (source == 1 ? C::bg0 : C::textDim);
        canvas.text (source == 1 ? "OUT" : "IN", labelFont_, visage::Font::kCenter,
                     bx + bw + 6.0f, by + 2.0f, bw, bh - 4.0f);
    }

    void drawSelectedStrip (visage::Canvas& canvas, unsigned int accent, unsigned int accentSoft)
    {
        if (selected_ < 0) return;
        auto b = processor_.getBand (selected_);
        if (!b.isValid()) return;

        const StripLayout s = stripLayout();
        canvas.setColor (C::surface);
        canvas.rectangle (0, s.y, width(), s.h);
        canvas.setColor (C::hairline);
        canvas.rectangle (0, s.y, width(), 1.0f);

        const int shape = (int)b.getProperty ("shape", (int)flush::Shape::Bell);
        const double f = b.getProperty ("freq", 1000.0);
        const double g = b.getProperty ("gain", 0.0);
        const double q = b.getProperty ("q", 1.0);
        const bool dyn = (bool)b.getProperty ("dynamic", false);
        const bool solo = (bool)b.getProperty ("solo", false);

        static constexpr const char* kShapeNames[] = {
            "Bell", "Notch", "Low Shelf", "High Shelf", "Low Cut",
            "High Cut", "Band Pass", "Tilt Shelf", "Flat Tilt", "All Pass"
        };

        // Type (click cycles).
        canvas.setColor (stripHover_ == 0 ? C::surface2 : C::surface);
        canvas.roundedRectangle (s.typeX, s.y + 4.0f, s.typeW, s.h - 8.0f, 4.0f);
        canvas.setColor (accent);
        canvas.text (kShapeNames[juce::jlimit (0, 9, shape)], smallFont_, visage::Font::kCenter,
                     s.typeX, s.y + 6.0f, s.typeW, s.h - 12.0f);

        drawScrub (canvas, 1, s.freqX, s.y, s.valW, "FREQ", freqText (f));
        drawScrub (canvas, 2, s.gainX, s.y, s.valW, "GAIN", gainText (g));
        drawScrub (canvas, 3, s.qX,    s.y, s.valW, "Q",    qText (q));

        // Dynamic / Solo toggles (anchored right-of-centre).
        canvas.setColor (dyn ? C::yellow : C::label);
        canvas.text ("DYN", smallFont_, visage::Font::kCenter, s.dynX, s.y + 5.0f, s.tglW, 10.0f);
        canvas.setColor (dyn ? C::yellow : C::surface2);
        canvas.circle (s.dynX + s.tglW * 0.5f - 5.0f, s.y + 16.0f, 10.0f);
        canvas.setColor (solo ? accentSoft : C::label);
        canvas.text ("SOLO", smallFont_, visage::Font::kCenter, s.soloX, s.y + 5.0f, s.tglW, 10.0f);
        canvas.setColor (solo ? accentSoft : C::surface2);
        canvas.circle (s.soloX + s.tglW * 0.5f - 5.0f, s.y + 16.0f, 10.0f);
    }

    void drawScrub (visage::Canvas& canvas, int field, float x, float y, float w,
                    const char* title, const juce::String& value)
    {
        canvas.setColor (stripHover_ == field ? C::surface2 : C::surface);
        canvas.roundedRectangle (x, y + 4.0f, w, stripH() - 8.0f, 4.0f);
        canvas.setColor (C::label);
        canvas.text (title, labelFont_, visage::Font::kCenter, x, y + 5.0f, w, 9.0f);
        canvas.setColor (C::text);
        canvas.text (value.toRawUTF8(), smallFont_, visage::Font::kCenter, x, y + 15.0f, w, 11.0f);
    }

    // ------------------------------------------------------------ helpers ----
    double displayRange() const
    {
        return { 3.0, 6.0, 12.0, 30.0 }[(int)*processor_.parameters.getRawParameterValue ("eq_scale")];
    }

    float freqAtX (float x) const
    {
        const float pw = plotW();
        const float t = clamp ((x - plotX()) / std::max (1.0f, pw), 0.0f, 1.0f);
        return (float)(20.0 * std::pow (1000.0, t));
    }

    static juce::String freqText (double f)
    {
        if (f >= 1000.0) return juce::String (f / 1000.0, 2) + " kHz";
        return juce::String ((int)f) + " Hz";
    }
    static juce::String gainText (double g)
    {
        return (g > 0.0 ? "+" : "") + juce::String (g, 1) + " dB";
    }
    static juce::String qText (double q)
    {
        return juce::String (q, 2);
    }

    int hitTest (float x, float y)
    {
        if (width() < 44.0f || height() < 30.0f) return -1;
        if (y > plotY() + plotH()) return -1;             // strip / labels are not nodes
        const double range = displayRange();
        const float px = plotX(), py = plotY(), pw = plotW(), ph = plotH();

        int best = -1; float bestD = 12.0f * 12.0f;
        const int count = juce::jmin ((int)cachedBands_.getNumChildren(), FlushAudioProcessor::kMaxBands);
        for (int i = 0; i < count; ++i) {
            const auto b = cachedBands_.getChild (i);
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

    // ------------------------------------------------------- mouse handlers --
    void mouseEnter (const visage::MouseEvent&) override { /* hover tracked in mouseMove */ }
    void mouseExit  (const visage::MouseEvent&) override { hovered_ = -1; redraw(); }

    void mouseMove (const visage::MouseEvent& e) override
    {
        if (dragging_ || scrubbing_ >= 0) return;
        int h = -1;
        int sh = -1;
        if (e.position.y <= plotY() + plotH()) {
            h = hitTest (e.position.x, e.position.y);
        } else if (e.position.y >= stripY() && selected_ >= 0) {
            // Strip hover (type = 0, freq/gain/q = 1..3).
            const StripLayout s = stripLayout();
            const float x = e.position.x;
            if (x >= s.typeX && x <= s.typeX + s.typeW) sh = 0;
            else if (x >= s.freqX && x <= s.freqX + s.valW) sh = 1;
            else if (x >= s.gainX && x <= s.gainX + s.valW) sh = 2;
            else if (x >= s.qX    && x <= s.qX + s.valW)    sh = 3;
        }
        if (h != hovered_ || sh != stripHover_) { hovered_ = h; stripHover_ = sh; redraw(); }
    }

    void mouseDown (const visage::MouseEvent& e) override
    {
        if (e.isRightButtonCurrentlyDown()) { showContextMenu (e); return; }
        if (!e.isLeftButtonCurrentlyDown()) return;

        // Analyzer toolbar (freeze + in/out) first — it floats over the plot.
        if (analyzerToolbarHit (e)) return;

        // Strip interactions next (type cycle / value scrub / dyn / solo).
        if (e.position.y >= stripY()) {
            stripMouseDown (e);
            return;
        }

        const auto now = juce::Time::getMillisecondCounter();
        const bool doubleClick = (now - lastClickTime_ < 400) &&
                                 (std::fabs (e.position.x - lastClickX_) < 6.0f) &&
                                 (std::fabs (e.position.y - lastClickY_) < 6.0f);
        lastClickTime_ = now;
        lastClickX_ = e.position.x;
        lastClickY_ = e.position.y;

        const int hit = hitTest (e.position.x, e.position.y);
        if (doubleClick) {
            if (hit >= 0) {
                auto b = processor_.getBand (hit);
                processor_.moveBand (hit, b.getProperty ("freq", 1000.0), 0.0);
            } else {
                processor_.addBand (freqAtX (e.position.x), 0.0, (int)flush::Shape::Bell);
            }
            redraw();
            return;
        }

        selected_ = hit;
        if (hit >= 0) {
            dragging_ = true;
            lastDrag_ = e.position;
            bubblePos_ = e.position;
            updateBubble (hit);
        }
        redraw();
    }

    void mouseDrag (const visage::MouseEvent& e) override
    {
        if (scrubbing_ >= 0) { scrubDrag (e); return; }
        if (!dragging_ || selected_ < 0 || width() < 45.0f) return;

        const float dx = e.position.x - lastDrag_.x;
        const float dy = e.position.y - lastDrag_.y;
        lastDrag_ = e.position;
        bubblePos_ = e.position;

        auto b = processor_.getBand (selected_);
        if (!b.isValid()) return;
        const float pw = plotW();
        const double range = displayRange();

        double f = b.getProperty ("freq", 1000.0);
        f = std::max (20.0, std::min (20000.0, f * std::pow (10.0, dx / pw * 3.0)));
        double g = b.getProperty ("gain", 0.0);
        g = std::max (-30.0, std::min (30.0, g - dy / plotH() * 2.0 * range));

        processor_.moveBand (selected_, f, g);
        updateBubble (selected_);
        redraw();
    }

    void mouseUp (const visage::MouseEvent&) override { dragging_ = false; scrubbing_ = -1; }

    bool mouseWheel (const visage::MouseEvent& e) override
    {
        const int hit = (e.position.y <= plotY() + plotH()) ? hitTest (e.position.x, e.position.y) : -1;
        if (hit < 0) return false;
        auto b = processor_.getBand (hit);
        if (!b.isValid()) return false;
        double q = b.getProperty ("q", 1.0);
        q *= std::pow (1.10, -e.wheel_delta_y * (e.isShiftDown() ? 0.2 : 1.0));
        processor_.setBandQ (hit, q);
        selected_ = hit;
        redraw();
        return true;
    }

    // ------------------------------------------------------------ keyboard ---
    bool keyPress (const visage::KeyEvent& e) override
    {
        const auto code = e.keyCode();
        if (code == visage::KeyCode::Delete || code == visage::KeyCode::Backspace) {
            if (selected_ >= 0) { processor_.removeBand (selected_); selected_ = -1; redraw(); }
            return true;
        }
        if (selected_ < 0) return false;

        auto b = processor_.getBand (selected_);
        if (!b.isValid()) return false;
        double f = b.getProperty ("freq", 1000.0);
        double g = b.getProperty ("gain", 0.0);
        const bool fine = e.isShiftDown();
        const double freqStep = fine ? 1.01 : 1.05;
        const double gainStep = fine ? 0.1 : 0.5;
        switch (code) {
            case visage::KeyCode::Left:  f /= freqStep; break;
            case visage::KeyCode::Right: f *= freqStep; break;
            case visage::KeyCode::Up:    g += gainStep; break;
            case visage::KeyCode::Down:  g -= gainStep; break;
            default: return false;
        }
        processor_.moveBand (selected_, f, g);
        redraw();
        return true;
    }

private:
    bool analyzerToolbarHit (const visage::MouseEvent& e)
    {
        const float pw = plotW();
        const float bx = plotX() + pw - 128.0f;
        const float by = plotY() + 4.0f;
        const float bw = 58.0f, bh = 16.0f;
        if (e.position.y < by || e.position.y > by + bh) return false;

        if (e.position.x >= bx && e.position.x <= bx + bw) {
            auto* fz = dynamic_cast<juce::AudioParameterBool*>
                       (processor_.parameters.getParameter ("analyzer_freeze"));
            if (fz) fz->setValueNotifyingHost (fz->get() ? 0.0f : 1.0f);
            redraw();
            return true;
        }
        if (e.position.x >= bx + bw + 6.0f && e.position.x <= bx + 2.0f * bw + 6.0f) {
            auto* src = dynamic_cast<juce::AudioParameterChoice*>
                        (processor_.parameters.getParameter ("analyzer_source"));
            if (src) src->setValueNotifyingHost ((float)((src->getIndex() + 1) % 2));
            redraw();
            return true;
        }
        return false;
    }

    // ------------------------------------------------ selected-band strip ----
    void stripMouseDown (const visage::MouseEvent& e)
    {
        if (selected_ < 0) return;
        const StripLayout s = stripLayout();
        const float x = e.position.x;

        // Type button.
        if (x >= s.typeX && x <= s.typeX + s.typeW) {
            auto b = processor_.getBand (selected_);
            const int shape = (int)b.getProperty ("shape", (int)flush::Shape::Bell);
            processor_.setBandType (selected_, (shape + 1) % 10);
            redraw();
            return;
        }
        // Dyn / Solo toggles.
        if (x >= s.dynX && x <= s.dynX + s.tglW) {
            auto b = processor_.getBand (selected_);
            processor_.setBandDynamic (selected_, !(bool)b.getProperty ("dynamic", false));
            redraw();
            return;
        }
        if (x >= s.soloX && x <= s.soloX + s.tglW) {
            auto b = processor_.getBand (selected_);
            processor_.setBandSolo (selected_, !(bool)b.getProperty ("solo", false));
            redraw();
            return;
        }
        // Freq/Gain/Q scrubbing.
        const float zones[3] = { s.freqX, s.gainX, s.qX };
        for (int fld = 0; fld < 3; ++fld) {
            if (x >= zones[fld] && x <= zones[fld] + s.valW) {
                scrubbing_ = fld;
                lastDrag_ = e.position;
                updateBubble (selected_);
                redraw();
                return;
            }
        }
    }

    void scrubDrag (const visage::MouseEvent& e)
    {
        if (scrubbing_ < 0 || selected_ < 0) return;
        auto b = processor_.getBand (selected_);
        if (!b.isValid()) return;
        const float dy = e.position.y - lastDrag_.y;
        lastDrag_ = e.position;
        bubblePos_ = e.position;

        if (scrubbing_ == 0) {       // frequency (log)
            double f = b.getProperty ("freq", 1000.0);
            f = std::max (20.0, std::min (20000.0, f * std::pow (10.0, -dy * 0.004)));
            processor_.setBandFreq (selected_, f);
        } else if (scrubbing_ == 1) { // gain
            double g = b.getProperty ("gain", 0.0);
            g = std::max (-30.0, std::min (30.0, g - dy * 0.15));
            processor_.setBandGain (selected_, g);
        } else {                      // Q
            double q = b.getProperty ("q", 1.0);
            q = std::max (0.05, std::min (40.0, q * std::pow (1.03, -dy)));
            processor_.setBandQ (selected_, q);
        }
        updateBubble (selected_);
        redraw();
    }

    void updateBubble (int index)
    {
        auto b = processor_.getBand (index);
        if (!b.isValid()) return;
        const double f = b.getProperty ("freq", 1000.0);
        const double g = b.getProperty ("gain", 0.0);
        const double q = b.getProperty ("q", 1.0);
        bubbleText_ = freqText (f) + "  " + gainText (g) + "  Q " + juce::String (q, 2);
    }

    // ------------------------------------------------------------ context menu --
    void showContextMenu (const visage::MouseEvent& e)
    {
        const int hit = hitTest (e.position.x, e.position.y);
        selected_ = hit;
        const float addFreq = freqAtX (e.position.x);
        const visage::Point menuPos = e.position;

        static constexpr const char* kShapeNames[] = {
            "Bell", "Notch", "Low Shelf", "High Shelf", "Low Cut",
            "High Cut", "Band Pass", "Tilt Shelf", "Flat Tilt", "All Pass"
        };

        contextMenu_ = visage::PopupMenu();
        if (hit >= 0) {
            visage::PopupMenu typeMenu ("Type");
            for (int s = 0; s < 10; ++s)
                typeMenu.addOption (1000 + s, visage::String (kShapeNames[s]));
            contextMenu_.addSubMenu (std::move (typeMenu));
            contextMenu_.addOption (2000, visage::String ("Dynamic"));
            contextMenu_.addOption (2001, visage::String ("Solo"));
            contextMenu_.addBreak();
            contextMenu_.addOption (2002, visage::String ("Remove Band"));
        } else {
            contextMenu_.addOption (3000, visage::String ("Add Band (Bell)"));
        }

        contextMenu_.onSelection().add ([this, addFreq] (int id) {
            if (id >= 1000 && id < 2000) {
                if (selected_ >= 0) processor_.setBandType (selected_, id - 1000);
            } else if (id == 2000 && selected_ >= 0) {
                auto b = processor_.getBand (selected_);
                processor_.setBandDynamic (selected_, !(bool)b.getProperty ("dynamic", false));
            } else if (id == 2001 && selected_ >= 0) {
                auto b = processor_.getBand (selected_);
                processor_.setBandSolo (selected_, !(bool)b.getProperty ("solo", false));
            } else if (id == 2002 && selected_ >= 0) {
                processor_.removeBand (selected_);
                selected_ = -1;
            } else if (id == 3000) {
                processor_.addBand (addFreq, 0.0, (int)flush::Shape::Bell);
            }
            redraw();
        });

        contextMenu_.show (this, menuPos);
    }

    FlushAudioProcessor& processor_;
    visage::Font labelFont_, smallFont_, valueFont_;
    visage::PopupMenu contextMenu_;
    juce::ValueTree cachedBands_;

    int selected_ = -1;
    int hovered_ = -1;
    int scrubbing_ = -1;
    int stripHover_ = -1;
    bool dragging_ = false;
    visage::Point lastDrag_;
    juce::uint32 lastClickTime_ = 0;
    float lastClickX_ = -1e9f, lastClickY_ = -1e9f;
    visage::Point bubblePos_;
    juce::String bubbleText_;
};

// ============================================================ Preset bar =====
// Compact horizontal toolbar (Pro-Q style) under the header: prev/next arrows,
// the current preset name, and A/B compare.
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
        canvas.setColor (visage::Brush::vertical (
            visage::Color (0xff1b1e24), visage::Color (0xff14161b)));
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
                     66.0f, 15.0f, std::max (40.0f, std::min (220.0f, width() - 300.0f)), 15.0f);

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
        canvas.setColor (active ? accentFor (processor_.parameters) : C::surface2);
        canvas.roundedRectangle (x, y, 46.0f, 16.0f, 3.0f);
        canvas.setColor (active ? C::bg0 : C::textDim);
        canvas.text (label, value_font_, visage::Font::kCenter, x, y + 1.0f, 46.0f, 14.0f);
    }

    FlushAudioProcessor& processor_;
    visage::Font label_font_, value_font_;
    int current_ = -1;
    int abSlot_ = -1;
};

// ============================================================ Settings gear ===
class FlushSettingsButton : public visage::Frame
{
public:
    explicit FlushSettingsButton (std::function<void()> onToggle) : onToggle_ (std::move (onToggle)) {}

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
        if (width() < 2.0f) return;

        canvas.setColor (0xcc000000);
        canvas.fill (0, 0, width(), height());

        const float pw = std::min (width(), 420.0f);
        const float ph = std::min (height(), 320.0f);
        const float px = (width() - pw) * 0.5f;
        const float py = (height() - ph) * 0.5f;

        canvas.setColor (visage::Brush::vertical (
            visage::Color (0xff2a3038), visage::Color (0xff22262d)));
        canvas.roundedRectangle (px, py, pw, ph, 8.0f);
        canvas.setColor (C::hairline);
        canvas.roundedRectangleBorder (px, py, pw, ph, 8.0f, 1.0f);

        canvas.setColor (C::text);
        canvas.text ("SETTINGS", title_font_, visage::Font::kTopLeft, px + 20.0f, py + 16.0f, 200.0f, 20.0f);

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
        canvas.setColor (accentFor (processor_.parameters));
        canvas.circle (knobX - 7.0f, sy + 16.0f, 14.0f);

        const float ry = sy + 56.0f;
        canvas.setColor (C::label);
        canvas.text ("UI REFRESH", label_font_, visage::Font::kTopLeft, sx, ry, 120.0f, 14.0f);
        const int fps = (int)*processor_.parameters.getRawParameterValue ("fps_mode");
        canvas.setColor (accentFor (processor_.parameters));
        canvas.text (juce::StringArray { "60", "120", "Uncapped" }[fps].toRawUTF8(),
                     value_font_, visage::Font::kTopRight, sx, ry, 120.0f, 16.0f);

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
        const float sx = px + 24.0f, sy = py + 56.0f;

        if (e.position.y >= sy + 16.0f && e.position.y <= sy + 30.0f) {
            dragging_ = true; dragScale (e); return;
        }
        const float ry = sy + 56.0f;
        if (e.position.y >= ry && e.position.y <= ry + 20.0f) {
            auto* c = dynamic_cast<juce::AudioParameterChoice*> (processor_.parameters.getParameter ("fps_mode"));
            if (c) c->setValueNotifyingHost ((float)((c->getIndex() + 1) % 3) / 2.0f);
            redraw(); return;
        }
        const float ay = ry + 40.0f;
        if (e.position.y >= ay + 18.0f && e.position.y <= ay + 38.0f) {
            const int i = (int)((e.position.x - (sx + 10.0f)) / 30.0f);
            if (i >= 0 && i < 4) {
                auto* c = dynamic_cast<juce::AudioParameterChoice*> (processor_.parameters.getParameter ("theme_accent"));
                if (c) c->setValueNotifyingHost ((float)i / 3.0f);
            }
            redraw();
        }

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
        const float px = (width() - pw) * 0.5f;
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

        // 0: preset bar.
        addChild (std::make_unique<FlushPresetBar> (processor));
        // 1-2: input.
        addChild (std::make_unique<FlushKnob> (processor.parameters, "input_gain", "INPUT TRIM"));
        addChild (std::make_unique<FlushMeter> (processor.parameters, processor.meterInRms, processor.meterInPeak));
        // 3: EQ display (kept raw for keyboard routing).
        eqDisplay_ = new FlushEqDisplay (processor);
        addChild (std::unique_ptr<FlushEqDisplay> (eqDisplay_));
        // 4-7: compressor.
        addChild (std::make_unique<FlushKnob> (processor.parameters, "comp_threshold", "THRESH"));
        addChild (std::make_unique<FlushKnob> (processor.parameters, "comp_ratio", "RATIO"));
        addChild (std::make_unique<FlushKnob> (processor.parameters, "comp_attack", "ATTACK"));
        addChild (std::make_unique<FlushKnob> (processor.parameters, "comp_release", "RELEASE"));
        // 8-11: Flush Match + output.
        addChild (std::make_unique<FlushChoiceButton> (processor.parameters, "flush_mode", "FLUSH MATCH"));
        addChild (std::make_unique<FlushKnob> (processor.parameters, "match_target", "TARGET"));
        addChild (std::make_unique<FlushKnob> (processor.parameters, "output_gain", "OUTPUT TRIM"));
        addChild (std::make_unique<FlushMeter> (processor.parameters, processor.meterOutRms, processor.meterOutPeak));
        // 12-16: header controls.
        addChild (std::make_unique<FlushChoiceButton> (processor.parameters, "eq_phase_mode", "PHASE"));
        addChild (std::make_unique<FlushChoiceButton> (processor.parameters, "meter_mode", "METER"));
        addChild (std::make_unique<FlushToggle> (processor.parameters, "analyzer_on", "ANALYZER"));
        addChild (std::make_unique<FlushToggle> (processor.parameters, "eq_enabled", "EQ"));
        addChild (std::make_unique<FlushSettingsButton> ([this] { setModalVisible (!modalVisible_); }));
        // 17: GR meter.
        addChild (std::make_unique<FlushGrMeter> (processor.meterGrDb));
        // 18: settings modal (overlay, drawn last).
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

    bool keyPress (const visage::KeyEvent& e) override
    {
        return eqDisplay_ ? eqDisplay_->processKeyPress (e) : false;
    }

    void resized() override
    {
        const float w = width(), h = height();
        const float header  = 46.0f;
        const float preseth = 28.0f;
        const float bodyY   = header + preseth;
        const float bodyH   = h - bodyY;

        const float inW   = w * 0.15f;
        const float eqW   = w * 0.42f;
        const float compW = w * 0.18f;
        const float outW  = w - inW - eqW - compW;

        auto child = [&](int i) { return children()[i]; };

        child (0)->setBounds (0.0f, header, w, preseth);
        child (1)->setBounds (inW * 0.5f - 26.0f, bodyY + 8.0f, 52.0f, 78.0f);
        child (2)->setBounds (inW * 0.5f - 16.0f, bodyY + 96.0f, 32.0f, bodyH - 120.0f);
        child (3)->setBounds (inW, bodyY, eqW, bodyH);

        const float compX = inW + eqW;
        for (int i = 0; i < 4; ++i)
            child (4 + i)->setBounds (compX + compW * 0.5f - 22.0f, bodyY + 12.0f + i * 92.0f, 44.0f, 66.0f);

        const float outX = compX + compW;
        child (8)->setBounds  (outX + 8.0f, bodyY + 8.0f, outW - 16.0f, 34.0f);
        child (9)->setBounds  (outX + outW * 0.5f - 22.0f, bodyY + 56.0f, 44.0f, 66.0f);
        child (10)->setBounds (outX + outW * 0.5f - 26.0f, bodyY + 132.0f, 52.0f, 78.0f);
        child (11)->setBounds (outX + outW - 40.0f, bodyY + 220.0f, 32.0f, bodyH - 244.0f);

        child (12)->setBounds (w - 364.0f, 6.0f, 96.0f, 36.0f);
        child (13)->setBounds (w - 262.0f, 6.0f, 96.0f, 36.0f);
        child (14)->setBounds (w - 160.0f, 6.0f, 52.0f, 36.0f);
        child (15)->setBounds (w - 102.0f, 6.0f, 52.0f, 36.0f);
        child (16)->setBounds (w - 44.0f, 6.0f, 40.0f, 36.0f);
        child (17)->setBounds (compX + compW - 26.0f, bodyY + 8.0f, 18.0f, bodyH - 40.0f);

        if (modal_ && modalVisible_)
            modal_->setBounds (0.0f, 0.0f, w, h);
    }

    void draw (visage::Canvas& canvas) override
    {
        // Panel background with a real vertical gradient.
        canvas.setColor (visage::Brush::vertical (
            visage::Color (C::bg1), visage::Color (C::bg0)));
        canvas.fill (0, 0, width(), height());

        canvas.setColor (C::stroke);
        canvas.fill (0, 0, width(), 46.0f);
        canvas.setColor (C::hairline);
        canvas.fill (0, 46.0f, width(), 1.0f);

        canvas.setColor (C::text);
        canvas.text ("FLUSH", title_font_, visage::Font::kTopLeft, 20.0f, 8.0f, 140.0f, 22.0f);
        canvas.setColor (C::label);
        canvas.text ("GAIN-STAGED DYNAMIC EQ", label_font_, visage::Font::kTopLeft, 20.0f, 32.0f, 240.0f, 12.0f);

        const float delta = processor.meterFlushDb.load();
        canvas.setColor (delta >= 0.0f ? C::green : C::yellow);
        canvas.text ((juce::String ("FLUSH ") + juce::String (delta, 1) + " dB").toRawUTF8(),
                     value_font_, visage::Font::kCenter, width() * 0.5f - 120.0f, 13.0f, 240.0f, 20.0f);

        const float latMs = processor.meterLatencyMs.load();
        canvas.setColor (C::textDim);
        canvas.text ((juce::String ("LATENCY ") + juce::String (latMs, 1) + " MS").toRawUTF8(),
                     label_font_, visage::Font::kTopRight, width() - 462.0f, 18.0f, 90.0f, 12.0f);
    }

private:
    FlushAudioProcessor& processor;
    visage::Font title_font_, label_font_, value_font_;
    FlushEqDisplay* eqDisplay_ = nullptr;
    FlushSettingsModal* modal_ = nullptr;
    bool modalVisible_ = false;
};
