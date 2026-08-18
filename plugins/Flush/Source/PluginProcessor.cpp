#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {

// ------------------------------------------------------------- tiny helpers --
constexpr double kPi = 3.14159265358979323846;

inline double dbToLin (double db)  { return std::pow (10.0, db / 20.0); }
inline double linToDb (double lin) { return (lin > 1e-12) ? 20.0 * std::log10 (lin) : -120.0; }

// Butterworth section Q for each 12 dB/oct of a cut filter.
constexpr double kCutQ = 0.7071067811865476;

inline juce::String floatToStringDb (float value, int) {
    return (value > 0.0f ? "+" : "") + juce::String (value, 1) + " dB";
}

} // namespace

//==============================================================================
FlushAudioProcessor::FlushAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, juce::Identifier ("FlushParams"), createParameterLayout())
{
    // Default band set (Pro-Q-style starting point), bands 9..24 disabled.
    struct DefaultBand { int shape; double freq, gain, q, slope; bool enabled; };
    const DefaultBand defaults[8] = {
        { (int)flush::Shape::LowCut,    20.0,   0.0, 1.0, 12.0, false },
        { (int)flush::Shape::LowShelf, 120.0,   0.0, 1.0, 12.0, true  },
        { (int)flush::Shape::Bell,     300.0,   0.0, 1.0, 12.0, true  },
        { (int)flush::Shape::Bell,    1000.0,   0.0, 1.0, 12.0, true  },
        { (int)flush::Shape::Bell,    3000.0,   0.0, 1.0, 12.0, true  },
        { (int)flush::Shape::Bell,    8000.0,   0.0, 1.0, 12.0, true  },
        { (int)flush::Shape::HighShelf, 12000.0, 0.0, 1.0, 12.0, true  },
        { (int)flush::Shape::HighCut,  20000.0,  0.0, 1.0, 12.0, false },
    };

    for (int i = 0; i < kMaxBands; ++i) {
        juce::ValueTree b ("BAND");
        b.setProperty ("id", i, nullptr);
        b.setProperty ("enabled", i < 8 ? defaults[i].enabled : false, nullptr);
        b.setProperty ("shape",  i < 8 ? defaults[i].shape  : (int)flush::Shape::Bell, nullptr);
        b.setProperty ("freq",   i < 8 ? defaults[i].freq   : 1000.0, nullptr);
        b.setProperty ("gain",   i < 8 ? defaults[i].gain   : 0.0,    nullptr);
        b.setProperty ("q",      i < 8 ? defaults[i].q      : 1.0,    nullptr);
        b.setProperty ("slope",  i < 8 ? defaults[i].slope  : 12.0,   nullptr);
        b.setProperty ("dynamic", false, nullptr);
        b.setProperty ("dynThr",  -24.0, nullptr);
        b.setProperty ("dynRange", 12.0, nullptr);
        b.setProperty ("dynAtk",   0.010, nullptr);
        b.setProperty ("dynRel",   0.150, nullptr);
        b.setProperty ("channel", 0, nullptr);
        b.setProperty ("solo", false, nullptr);
        bandTree.appendChild (b, nullptr);
    }

    bandsDirty_.store (true);
    validateParameterIds();
}

FlushAudioProcessor::~FlushAudioProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout FlushAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // --- Level / Flush Match ---
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "input_gain", 1 }, "Input Trim",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, "dB",
        juce::AudioProcessorParameter::genericParameter, floatToStringDb));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "flush_mode", 1 }, "Flush Match",
        juce::StringArray { "Off", "Match Input", "Target" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "flush_speed", 1 }, "Match Timing",
        juce::StringArray { "Slow (Bus)", "Medium", "Fast" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "flush_reference", 1 }, "Loudness Reference",
        juce::StringArray { "Mid (mono-sum)", "Stereo (Mid+Side)" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "match_target", 1 }, "Target Level",
        juce::NormalisableRange<float> (-30.0f, 0.0f, 0.1f), -18.0f, "dB",
        juce::AudioProcessorParameter::genericParameter, floatToStringDb));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output_gain", 1 }, "Output Trim",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, "dB",
        juce::AudioProcessorParameter::genericParameter, floatToStringDb));

    // --- Global EQ ---
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "eq_enabled", 1 }, "EQ On", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "eq_phase_mode", 1 }, "Phase Mode",
        juce::StringArray { "Low Latency", "Natural", "Linear" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "eq_linear_quality", 1 }, "Linear Quality",
        juce::StringArray { "Low", "Medium", "High", "Max" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "eq_oversample", 1 }, "Oversampling",
        juce::StringArray { "Off", "2x", "4x" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "eq_scale", 1 }, "Display Range",
        juce::StringArray { "3 dB", "6 dB", "12 dB", "30 dB" }, 2));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "eq_gain_q_link", 1 }, "Gain-Q Link", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "eq_phase_invert", 1 }, "Phase Invert", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "eq_piano", 1 }, "Piano Display", false));

    // --- Compressor (broadband instance of the shared Dynamics Engine) ---
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "comp_enabled", 1 }, "Comp On", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "comp_mode", 1 }, "Comp Mode",
        juce::StringArray { "Broadband", "Mid/Side", "Spectral" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "comp_threshold", 1 }, "Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -18.0f, "dB",
        juce::AudioProcessorParameter::genericParameter, floatToStringDb));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "comp_ratio", 1 }, "Ratio",
        juce::NormalisableRange<float> (1.0f, 20.0f, 0.1f, 0.5f), 4.0f, ":1"));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "comp_attack", 1 }, "Attack",
        juce::NormalisableRange<float> (0.1f, 100.0f, 0.1f, 0.4f), 10.0f, "ms"));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "comp_release", 1 }, "Release",
        juce::NormalisableRange<float> (10.0f, 2000.0f, 1.0f, 0.4f), 150.0f, "ms"));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "comp_detector", 1 }, "Detector",
        juce::StringArray { "Peak", "RMS" }, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "comp_makeup", 1 }, "Makeup",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, "dB",
        juce::AudioProcessorParameter::genericParameter, floatToStringDb));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "comp_auto_makeup", 1 }, "Auto Makeup", true));

    // --- Display ---
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "meter_mode", 1 }, "Meter Style",
        juce::StringArray { "Bar (Vertical)", "VU (Needle)" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "analyzer_on", 1 }, "Analyzer", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "analyzer_speed", 1 }, "Analyzer Speed",
        juce::StringArray { "Slow", "Medium", "Fast" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "analyzer_range", 1 }, "Analyzer Range",
        juce::StringArray { "60 dB", "90 dB", "120 dB" }, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "analyzer_tilt", 1 }, "Analyzer Tilt",
        juce::NormalisableRange<float> (0.0f, 6.0f, 0.1f), 4.5f, "dB/oct"));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "analyzer_freeze", 1 }, "Analyzer Freeze", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "analyzer_source", 1 }, "Analyzer Source",
        juce::StringArray { "Input", "Output" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "ui_scale", 1 }, "UI Scale",
        juce::NormalisableRange<float> (0.5f, 2.0f, 0.05f), 1.0f, "x"));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "fps_mode", 1 }, "UI Refresh",
        juce::StringArray { "60", "120", "Uncapped" }, 2));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "theme_accent", 1 }, "Accent",
        juce::StringArray { "Blue", "Cyan", "Orange", "Violet" }, 0));

    return layout;
}

//==============================================================================
void FlushAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sampleRate_ = sampleRate;

    // Guard degenerate host reports so the DSP never divides by zero.
    if (!(sampleRate > 0.0)) sampleRate = 44100.0;
    if (samplesPerBlock <= 0) samplesPerBlock = 512;

    // Oversampling sets the internal processing rate: 1x / 2x / 4x (EQ section).
    oversampleMode_ = juce::jlimit (0, 2, paramI ("eq_oversample"));   // 0/1/2
    procRate_ = sampleRate * (double)(1 << oversampleMode_);

    inputGainSmooth_.setTau (0.005, sampleRate);
    outputGainSmooth_.setTau (0.005, sampleRate);

    compressor_.reset (sampleRate);
    compressorSide_.reset (sampleRate);
    compDelayL_.setDelay (0);
    compDelayR_.setDelay (0);
    compDelayM_.setDelay (0);
    compDelayS_.setDelay (0);
    flushMatch_.reset (sampleRate);
    flushMatch_.setTiming (paramI ("flush_speed"));

    tpInL_.reset (sampleRate); tpInR_.reset (sampleRate);
    tpOutL_.reset (sampleRate); tpOutR_.reset (sampleRate);

    analyzer_.reset (sampleRate);
    analyzer_.setSpeed (paramI ("analyzer_speed", 1));
    analyzer_.setTilt ((double)paramF ("analyzer_tilt", 4.5f));
    analyzer_.setRange ({ 60.0, 90.0, 120.0 }[juce::jlimit (0, 2, paramI ("analyzer_range", 1))]);
    analyzerScratch_.resize (samplesPerBlock);
    analyzerScratchOut_.resize (samplesPerBlock);

    osM_.reset (sampleRate);
    osS_.reset (sampleRate);
    os4M_.reset (sampleRate);
    os4S_.reset (sampleRate);

    spectralL_.reset (sampleRate, 2048);
    spectralR_.reset (sampleRate, 2048);

    linearFirDirty_.store (true);
    refreshBands (procRate_);
    updateLatency();
}

void FlushAudioProcessor::releaseResources()
{
    for (auto& b : bands_) {
        b.sectionsM.clear();
        b.sectionsS.clear();
        b.coefs.clear();
    }
}

bool FlushAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

//==============================================================================
int FlushAudioProcessor::phaseMode() const
{
    // Const-safe null-guarded read (the ID is validated at construction, but a
    // defensive nullptr check costs nothing and can't deref a missing param).
    auto* p = parameters.getRawParameterValue ("eq_phase_mode");
    return p ? juce::jlimit (0, 2, (int)p->load()) : 0;
}

float* FlushAudioProcessor::rawParam (const juce::String& id)
{
    auto* p = parameters.getRawParameterValue (id);
    jassert (p != nullptr);                       // unknown ID -> visible in debug
    return p ? p : &paramFallback_;
}

float FlushAudioProcessor::paramF (const juce::String& id, float fallback)
{
    auto* p = parameters.getRawParameterValue (id);
    jassert (p != nullptr);
    return p ? p->load() : fallback;
}

int FlushAudioProcessor::paramI (const juce::String& id, int fallback)
{
    auto* p = parameters.getRawParameterValue (id);
    jassert (p != nullptr);
    return p ? (int)p->load() : fallback;
}

bool FlushAudioProcessor::paramB (const juce::String& id, bool fallback)
{
    auto* p = parameters.getRawParameterValue (id);
    jassert (p != nullptr);
    return p ? (p->load() > 0.5f) : fallback;
}

void FlushAudioProcessor::validateParameterIds()
{
    // jasserts every parameter ID the audio path depends on exists. A typo here
    // becomes a hard failure in debug builds instead of a null-deref crash in
    // release. Called once from the constructor.
    static constexpr const char* ids[] = {
        "input_gain", "output_gain", "flush_mode", "flush_speed", "flush_reference",
        "match_target", "eq_enabled", "eq_phase_mode", "eq_linear_quality",
        "eq_oversample", "eq_scale", "eq_gain_q_link", "eq_phase_invert", "eq_piano",
        "comp_enabled", "comp_mode", "comp_threshold", "comp_ratio", "comp_attack",
        "comp_release", "comp_detector", "comp_makeup", "comp_auto_makeup",
        "meter_mode", "analyzer_on", "analyzer_speed", "analyzer_range",
        "analyzer_tilt", "analyzer_freeze", "analyzer_source",
        "ui_scale", "fps_mode", "theme_accent"
    };
    for (auto* id : ids)
        jassert (parameters.getParameter (juce::String (id)) != nullptr);
}

void FlushAudioProcessor::updateLatency()
{
    double latencyBase = 0.0;

    // Linear-phase FIR group delay, converted to base-rate samples.
    if (linearActive_ && linearReady_)
        latencyBase += (double)linearFirM_.latency() / (double)(1 << oversampleMode_);

    // Oversampling round-trip delay (up + down), in base samples.
    if (oversampleMode_ == 1)
        latencyBase += (double)osM_.latencySamples();
    else if (oversampleMode_ == 2)
        latencyBase += (double)os4M_.latencySamples();

    // Spectral dynamics overlap-add latency.
    if (spectralActive_)
        latencyBase += (double)spectralL_.latency();

    // Compressor lookahead (broadband mode only — spectral mode has its own
    // overlap-add latency and doesn't use the time-domain engine). The audio is
    // delayed by this many samples and the undelayed GR applied to it.
    if (!spectralActive_)
        latencyBase += (double)compressor_.lookaheadLatencySamples();

    // Clamp to a sane non-negative value (defends against any path that could
    // produce a negative/NaN latency and confuse the host).
    if (!(latencyBase >= 0.0) || !std::isfinite (latencyBase))
        latencyBase = 0.0;

    setLatencySamples ((int)std::lround (latencyBase));
    meterLatencyMs.store ((float)(latencyBase * 1000.0 / std::max (1.0, sampleRate_)));
}

//==============================================================================
void FlushAudioProcessor::buildBand (int index, const juce::ValueTree& b,
                                     double sampleRate, bool natural)
{
    auto& br = bands_[index];
    br.enabled     = (bool)b.getProperty ("enabled", true);
    br.shape       = (flush::Shape)(int)b.getProperty ("shape", (int)flush::Shape::Bell);
    br.freq        = std::max (10.0,  std::min (sampleRate * 0.48, (double)b.getProperty ("freq", 1000.0)));
    br.gainDb      = (double)b.getProperty ("gain", 0.0);
    br.q           = std::max (0.05, std::min (40.0, (double)b.getProperty ("q", 1.0)));
    br.slopeDbOct  = (double)b.getProperty ("slope", 12.0);

    // Gain-Q interaction (Pro-Q "Gain-Q Link"): bandwidth narrows as |gain| grows.
    if (paramB ("eq_gain_q_link", false))
        br.q = flush::gainQLinkedQ (br.q, br.gainDb);

    // Clamp every band property read from state: a malformed preset / old state
    // file must degrade gracefully, never produce NaN/Inf or out-of-range DSP.
    br.dynamic     = (bool)b.getProperty ("dynamic", false);
    br.dynThresholdDb = std::max (-120.0, std::min (0.0,   (double)b.getProperty ("dynThr", -24.0)));
    br.dynRangeDb  = std::max (0.0,    std::min (60.0,  (double)b.getProperty ("dynRange", 12.0)));
    br.dynAttackSec = std::max (0.0001, std::min (1.0,   (double)b.getProperty ("dynAtk", 0.010)));
    br.dynReleaseSec = std::max (0.001,  std::min (5.0,   (double)b.getProperty ("dynRel", 0.150)));
    br.channel     = juce::jlimit (0, 2, (int)b.getProperty ("channel", 0));
    br.solo        = (bool)b.getProperty ("solo", false);

    // Real-time safety: a band is at most 4 sections (12-48 dB/oct cuts) or 1
    // (every other shape). Reserving that upper bound means the FIRST build (in
    // prepareToPlay, off the audio thread) allocates once, and subsequent
    // rebuilds triggered by UI node drags reuse the capacity — allocation-free
    // on the audio thread.
    br.sectionsM.reserve (4);
    br.sectionsS.reserve (4);
    br.coefs.reserve (4);

    const double fs = sampleRate, f0 = br.freq, gain = br.gainDb, Q = br.q;

    // Phase-mode routing (no-cramp matched/decramped designs only — RBJ is a
    // measurement baseline, never a user-facing path):
    //   Low Latency -> Vicanek matched (2016)
    //   Natural     -> Orfanidis decramped (the Pro-Q "high-freq decramping")
    //   Linear      -> matched for now; FIR path is the next implementation pass
    const bool orfanidisMode = natural;

    // Compute the target coefficient list for this band first; then either MORPH
    // the existing sections to it (click-free parameter changes — node drags,
    // automation) or rebuild from scratch only when the structure changed
    // (band type / slope altering the section count).
    std::vector<flush::BiquadCoef> newCoefs;
    newCoefs.reserve (4);
    auto pushSection = [&](flush::BiquadCoef c) { newCoefs.push_back (c); };

    // Cuts use the Vicanek MATCHED (decramped) LP/HP, NOT the bilinear SVF: the
    // bilinear design re-cramps the high-frequency response near Nyquist, which
    // is exactly the artifact the rest of the EQ (matched bells/shelves) already
    // avoids. Every audible-magnitude shape in Flush is now decramped; double
    // precision + sanitize() cover the low-frequency stability the SVF used to
    // provide.

    switch (br.shape) {
        case flush::Shape::Bell:
            pushSection (orfanidisMode ? flush::orfanidis::bell (fs, f0, gain, Q)
                                       : flush::matched::bell (fs, f0, gain, Q));
            break;

        case flush::Shape::LowShelf:
            // Decramped matched one-pole shelf (Vicanek 2019) — transparent,
            // no droop near Nyquist (replaces the commodity RBJ shelf).
            pushSection (flush::shelfMatch::lowShelf (fs, f0, gain));
            break;

        case flush::Shape::HighShelf:
            pushSection (flush::shelfMatch::highShelf (fs, f0, gain));
            break;

        case flush::Shape::LowCut: {
            // "Low Cut" = cut the LOWS = high-pass filter (decramped matched).
            // Slope 12-48 dB/oct => 1-4 cascaded 2nd-order sections.
            const int n = std::max (1, std::min (4, (int)std::lround (br.slopeDbOct / 12.0)));
            for (int i = 0; i < n; ++i)
                pushSection (flush::matched::highpass (fs, f0, kCutQ));
            break;
        }

        case flush::Shape::HighCut: {
            // "High Cut" = cut the HIGHS = low-pass filter (decramped matched).
            const int n = std::max (1, std::min (4, (int)std::lround (br.slopeDbOct / 12.0)));
            for (int i = 0; i < n; ++i)
                pushSection (flush::matched::lowpass (fs, f0, kCutQ));
            break;
        }

        case flush::Shape::BandPass:
            pushSection (flush::matched::bandpass (fs, f0, Q));
            break;

        case flush::Shape::Notch:
            // RBJ notch: its zeros sit ON the unit circle -> a perfect null at
            // the center frequency (the notch's most important property). No
            // matched/decramped notch exists in the public literature I have;
            // a matched-bandpass-based notch only nulls ~ -20 dB (the bandpass
            // has non-zero phase at center), which is not an acceptable notch.
            pushSection (flush::rbj::notch (fs, f0, Q));
            break;

        case flush::Shape::AllPass:
            pushSection (flush::rbj::allpass (fs, f0, Q));
            break;

        case flush::Shape::TiltShelf:
            // v1 approximation: a tilt implemented as a steep shelf (refine in later pass).
            pushSection (flush::shelfMatch::highShelf (fs, f0, gain));
            break;

        case flush::Shape::FlatTilt:
            pushSection (flush::firstOrderTilt (fs, f0, gain));
            break;
    }

    // Apply the new coefficients: morph in place when the section count is
    // unchanged (the common case — frequency/gain/Q changes on a bell/shelf),
    // rebuild only when the structure changed (type/slope changed the count).
    if (!br.sectionsM.empty() && br.sectionsM.size() == newCoefs.size()) {
        for (size_t i = 0; i < newCoefs.size(); ++i) {
            br.sectionsM[i].setCoeffs (newCoefs[i], 64);   // 64-sample crossfade
            br.sectionsS[i].setCoeffs (newCoefs[i], 64);
        }
        br.coefs = std::move (newCoefs);
    } else {
        br.sectionsM.clear();
        br.sectionsS.clear();
        for (const auto& c : newCoefs) {
            flush::MorphingBiquad bm; bm.setCoeffs (c, 0);
            flush::MorphingBiquad bs; bs.setCoeffs (c, 0);
            br.sectionsM.push_back (bm);
            br.sectionsS.push_back (bs);
        }
        br.coefs = std::move (newCoefs);
    }

    if (br.dynamic) {
        // A dynamic band IS a band-limited compressor: the band-filtered signal
        // feeds the shared Dynamics Engine and the gain is applied through the
        // same band. Oxford-style adaptive release makes heavy GR "hang" and
        // light GR let go — musical, transparent, never pumping.
        flush::DynamicsParams p;
        p.thresholdDb = br.dynThresholdDb;
        p.ratio       = 40.0;               // dynamic bands behave ~ full-ratio
        p.attackSec   = br.dynAttackSec;
        p.releaseSec  = br.dynReleaseSec;
        p.kneeDb      = 6.0;
        p.rangeDb     = br.dynRangeDb;
        p.rms         = false;
        p.adaptiveRelease = true;
        p.adaptiveAmount  = 4.0;
        // NOTE: dynamic bands deliberately use NO lookahead — a short attack +
        // adaptive release is the standard (and correct) dynamic-EQ behaviour;
        // lookahead is a broadband limiter/compressor technique (Pro-Q does the
        // same: dynamic bands have attack/release, no lookahead).
        // Reset the detector/ballistics state at the processing rate. NOTE: the
        // envelope state does not migrate across rebuilds (a coefficient edit
        // briefly re-triggers the band) — state migration is a later refinement.
        br.dyn.reset (sampleRate);
        br.dyn.setParams (p);
    }
}

void FlushAudioProcessor::refreshBands (double sampleRate)
{
    const bool natural = (phaseMode() == 1);
    {
        // The band tree is written by the UI thread; snapshot it under the lock
        // so a concurrent node drag / preset load can't race the rebuild.
        std::lock_guard<std::mutex> lock (bandMutex_);
        for (int i = 0; i < kMaxBands; ++i)
            buildBand (i, bandTree.getChild (i), sampleRate, natural);
    }
    bandsDirty_.store (false);
    publishUiSnapshot (sampleRate);
}

// Publish a UI snapshot of the band coefficients + enabled state + processing
// rate. The UI draws the EQ curve from this snapshot under uiMutex_, never from
// the live audio buffers, so a curve repaint can't race an in-place rebuild.
void FlushAudioProcessor::publishUiSnapshot (double sampleRate)
{
    std::lock_guard<std::mutex> lock (uiMutex_);
    uiProcRate_ = sampleRate;
    for (int i = 0; i < kMaxBands; ++i) {
        uiEnabled_[i] = bands_[i].enabled;
        uiCoefs_[i]   = bands_[i].coefs;
        uiChannel_[i] = bands_[i].channel;
    }
}

// Process one channel's cascade, returning the filtered sample.
double FlushAudioProcessor::processBandChannel (std::vector<flush::MorphingBiquad>& sections, double in)
{
    double out = in;
    for (auto& s : sections)
        out = s.process (out);
    return out;
}

// Complex responses of the static EQ's MID and SIDE paths at frequency f (Hz).
//   H_mid(w) = 1 + Σ (H_i - 1)  over static bands placed stereo or mid
//   H_side(w)= 1 + Σ (H_i - 1)  over static bands placed stereo or side
void FlushAudioProcessor::staticPathResponses (double f, flush::Complex& mid, flush::Complex& side) const
{
    const double w = 2.0 * kPi * f / procRate_;
    double mre = 1.0, mim = 0.0, sre = 1.0, sim = 0.0;
    for (int i = 0; i < kMaxBands; ++i) {
        const auto& br = bands_[i];
        if (!br.enabled || br.dynamic) continue;   // static only

        flush::Complex h = flush::cascadeResponse (br.coefs, w);

        const double dRe = h.re - 1.0, dIm = h.im;
        if (br.channel == 0) {                       // stereo -> both paths
            mre += dRe; mim += dIm;
            sre += dRe; sim += dIm;
        } else if (br.channel == 1) {                // mid only
            mre += dRe; mim += dIm;
        } else if (br.channel == 2) {                // side only
            sre += dRe; sim += dIm;
        }
    }
    mid = { mre, mim };
    side = { sre, sim };
}

// Rebuild the linear-phase FIRs: one for the MID path, one for the SIDE path
// (so per-band Mid/Side placement is honoured in Linear mode, not collapsed
// into a single stereo filter).
void FlushAudioProcessor::rebuildLinearFir()
{
    linearReady_ = false;

    auto midMag = [&](double f) {
        flush::Complex m, s;
        staticPathResponses (f, m, s);
        return std::hypot (m.re, m.im);
    };
    auto sideMag = [&](double f) {
        flush::Complex m, s;
        staticPathResponses (f, m, s);
        return std::hypot (s.re, s.im);
    };

    linearCoeffs_ = flush::designLinearPhaseFir (midMag, procRate_, firLength_, 6.0);
    linearFirM_.set (linearCoeffs_);
    linearCoeffs_ = flush::designLinearPhaseFir (sideMag, procRate_, firLength_, 6.0);
    linearFirS_.set (linearCoeffs_);
    linearFirDirty_.store (false);
    linearReady_ = true;
}

void FlushAudioProcessor::processEqSample (double& m, double& s)
{
    // PARALLEL topology (Pro-Q style): every band filters the DRY signal and
    // contributes a delta; out = dry + Σ delta_i. (The previous serial cascade
    // let bands interact — band N filtered band N-1's already-modified signal,
    // which is both a sound-quality difference and the reason band soloing
    // couldn't work correctly.)
    bool anySolo = false;
    for (int i = 0; i < kMaxBands; ++i)
        if (bands_[i].enabled && bands_[i].solo) { anySolo = true; break; }

    const double dryM = m, dryS = s;
    double outM = anySolo ? 0.0 : dryM;   // solo mode: hear ONLY the soloed deltas
    double outS = anySolo ? 0.0 : dryS;

    for (int i = 0; i < kMaxBands; ++i) {
        auto& br = bands_[i];
        if (!br.enabled) continue;
        if (anySolo && !br.solo) continue;

        // In linear-phase mode the static bands are handled by the combined FIR;
        // only the (minimum-phase) dynamic bands pass through this loop.
        if (linearActive_ && !br.dynamic) continue;

        // Skip identity bands (0 dB gain, static, shapes that are exactly wire
        // at 0 dB). Saves running 24 biquads when most bands are flat (the
        // default preset starts with every band at 0 dB).
        if (!br.dynamic && std::fabs (br.gainDb) < 1e-9) {
            switch (br.shape) {
                case flush::Shape::Bell:
                case flush::Shape::LowShelf:
                case flush::Shape::HighShelf:
                case flush::Shape::TiltShelf:
                case flush::Shape::FlatTilt:
                    continue;
                default: break;   // cuts/notch/band-pass are NOT identity at 0 dB
            }
        }

        const bool useM = (br.channel == 0 || br.channel == 1);   // stereo | mid
        const bool useS = (br.channel == 0 || br.channel == 2);   // stereo | side

        // Filter the DRY signal at the band's full static gain.
        double bandM = dryM, bandS = dryS;
        if (useM) bandM = processBandChannel (br.sectionsM, dryM);
        if (useS) bandS = processBandChannel (br.sectionsS, dryS);

        double k = 1.0;
        if (br.dynamic) {
            // Sidechain = band-limited signal at full static gain (no feedback).
            double sc = (useM && useS) ? 0.5 * (bandM + bandS)
                       : useM         ? bandM
                                      : bandS;
            const double gr = br.dyn.processGrDb (sc);
            k = dbToLin (-gr);
        }

        if (useM) outM += k * (bandM - dryM);
        if (useS) outS += k * (bandS - dryS);
    }

    m = outM;
    s = outS;
}

//==============================================================================
void FlushAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalIn  = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();

    // Channel edge cases: no outputs -> nothing to do; no inputs -> output
    // silence and leave (so getWritePointer is never called with an invalid
    // index or on an empty bus).
    if (totalOut <= 0)
        return;
    if (totalIn <= 0) {
        buffer.clear();
        return;
    }
    for (int i = totalIn; i < totalOut; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (buffer.getNumSamples() == 0)
        return;

    // --- parameter snapshot (null-safe: an unknown ID yields a sane fallback,
    //    never a crash) ------------------------------------------------------
    const bool   eqOn      = paramB ("eq_enabled", true);
    const bool   compOn    = paramB ("comp_enabled", true);
    const bool   invert    = paramB ("eq_phase_invert", false);
    const float  inGainDb  = paramF ("input_gain");
    const float  outGainDb = paramF ("output_gain");
    const int    compDet   = paramI ("comp_detector");
    const bool   autoMk    = paramB ("comp_auto_makeup", true);
    const float  compMk    = paramF ("comp_makeup");
    const int    flushMode = paramI ("flush_mode", 1);
    const int    flushRef  = paramI ("flush_reference");
    const int    flushSpd  = paramI ("flush_speed");
    const float  targetDb  = paramF ("match_target", -18.0f);

    // Compressor mode this block (0 broadband, 1 mid/side, 2 spectral) — computed
    // EARLY so updateLatency() reports the right latency without a one-block lag.
    const int compMode = paramI ("comp_mode");
    spectralActive_ = compOn && (compMode == 2);
    msActive_ = compOn && (compMode == 1);

    // --- phase mode / oversampling / band rebuild ---------------------------
    linearActive_ = (phaseMode() == 2);

    // Linear-phase FIR quality (latency/accuracy trade-off), Pro-Q style:
    // Low/Medium/High/Max -> FIR length -> group delay.
    const int firQuality = paramI ("eq_linear_quality", 1);
    if (firQuality != lastFirQuality_) {
        static constexpr int kFirLengths[4] = { 511, 1023, 2047, 4095 };
        firLength_ = kFirLengths[juce::jlimit (0, 3, firQuality)];
        lastFirQuality_ = firQuality;
        linearFirDirty_.store (true);
    }

    const int oversampleNow = juce::jlimit (0, 2, paramI ("eq_oversample"));  // 0/1/2
    if (oversampleNow != oversampleMode_ || phaseMode() != lastPhaseMode_) {
        oversampleMode_ = oversampleNow;
        procRate_ = sampleRate_ * (1 << oversampleMode_);
        osM_.reset (sampleRate_); osS_.reset (sampleRate_);
        os4M_.reset (sampleRate_); os4S_.reset (sampleRate_);
        lastPhaseMode_ = phaseMode();
        bandsDirty_.store (true);
        linearFirDirty_.store (true);
    }
    if (bandsDirty_.load())
        refreshBands (procRate_);
    if (linearActive_) {
        if (linearFirDirty_.load())
            rebuildLinearFir();
    } else {
        linearReady_ = false;
    }

    // --- analyzer configuration (display only, runtime-changeable) ----------
    if (paramB ("analyzer_on", true)) {
        analyzer_.setSpeed (paramI ("analyzer_speed", 1));
        analyzer_.setTilt ((double)paramF ("analyzer_tilt", 4.5f));
        analyzer_.setFreeze (paramB ("analyzer_freeze", false));
    }

    // --- compressor / spectral dynamics (shared threshold/ratio/attack/release) ---
    if (spectralActive_) {
        flush::SpectralParams sp;
        sp.thresholdDb = paramF ("comp_threshold", -18.0f);
        sp.ratio       = paramF ("comp_ratio", 4.0f);
        sp.rangeDb     = 24.0;
        sp.attackSec   = paramF ("comp_attack", 10.0f) / 1000.0;
        sp.releaseSec  = paramF ("comp_release", 150.0f) / 1000.0;
        sp.kneeDb      = 6.0;
        spectralL_.setParams (sp);
        spectralR_.setParams (sp);
    } else {
        flush::DynamicsParams p;
        p.thresholdDb = paramF ("comp_threshold", -18.0f);
        p.ratio       = paramF ("comp_ratio", 4.0f);
        p.attackSec   = paramF ("comp_attack", 10.0f) / 1000.0;
        p.releaseSec  = paramF ("comp_release", 150.0f) / 1000.0;
        p.kneeDb      = 6.0;
        p.rangeDb     = 60.0;
        p.rms         = (compDet == 1);
        p.adaptiveRelease = true;   // transparent: heavy GR hangs, light GR lets go
        p.adaptiveAmount  = 4.0;
        p.lookaheadSec    = 0.0015; // 1.5 ms lookahead: the AUDIO is delayed so the
                                    // attack never "grabs" transients — the
                                    // Oxford-style transparent attack.
        compressor_.setParams (p);
        compressorSide_.setParams (p);

        // Reconfigure the audio delay lines to match the lookahead (allocation
        // happens here on the audio thread, but only when the delay length
        // actually changes — it is a fixed 72 samples otherwise).
        const int la = compressor_.lookaheadLatencySamples();
        if (la != compDelayL_.delay) {
            compDelayL_.setDelay (la);
            compDelayR_.setDelay (la);
            compDelayM_.setDelay (la);
            compDelayS_.setDelay (la);
        }
    }

    // Recompute latency AFTER the compressor config so the lookahead is counted
    // without a one-block lag.
    updateLatency();

    // --- Flush Match configuration -----------------------------------------
    flushMatch_.setMode ((flush::FlushMatch::Mode)flushMode);
    flushMatch_.setReference ((flush::FlushMatch::Reference)flushRef);
    flushMatch_.setTargetDb (targetDb);
    flushMatch_.setTiming (flushSpd);

    // --- process ------------------------------------------------------------
    // Channel safety: guard against zero in/out buses and against output
    // channels aliasing input channels (mono->stereo etc.).
    const bool stereoOut = totalOut > 1;
    const bool stereoIn  = totalIn > 1;
    auto* l = buffer.getWritePointer (0);
    auto* r = stereoOut ? buffer.getWritePointer (1) : l;

    const double targetInGain  = dbToLin (inGainDb);
    const double targetOutGain = dbToLin (outGainDb);

    double inPeak = 0.0, inSumSq = 0.0, outPeak = 0.0, outSumSq = 0.0;
    double midSumSq = 0.0, sideSumSq = 0.0, midSideDot = 0.0;
    double makeupDb = compMk;

    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        const double inL = (totalIn > 0) ? l[i] : 0.0;
        const double inR = stereoIn ? r[i] : inL;         // mono -> duplicate

        // Input trim (short smoothed ramp to avoid zipper).
        const double inGainLin = inputGainSmooth_.update (targetInGain);
        double L = inL * inGainLin;
        double R = inR * inGainLin;

        // Input metering + M/S/correlation (pre-EQ, the source's phase state).
        const double m = 0.5 * (L + R);
        const double s = 0.5 * (L - R);
        inPeak = std::max (inPeak, std::max (tpInL_.process (L), tpInR_.process (R)));
        inSumSq += L * L + R * R;
        midSumSq  += m * m;
        sideSumSq += s * s;
        midSideDot += m * m - s * s;

        // Analyzer input tap (display only).
        if ((int)analyzerScratch_.size() > i)
            analyzerScratch_[(size_t)i] = m;          // mono mix = mid

        // EQ (24-band dynamic, M/S domain) — optional 2x oversampling and
        // linear-phase FIR for the static bands.
        if (eqOn) {
            auto eqPass = [&](double& mm, double& ss) {
                if (linearActive_ && linearReady_) {
                    mm = linearFirM_.process (mm);
                    ss = linearFirS_.process (ss);
                }
                processEqSample (mm, ss);
            };

            double M = m, S = s;
            if (oversampleMode_ == 1) {
                double upM[2], upS[2];
                osM_.upsample (M, upM); osS_.upsample (S, upS);
                eqPass (upM[0], upS[0]); eqPass (upM[1], upS[1]);
                M = osM_.downsample (upM[0], upM[1]);
                S = osS_.downsample (upS[0], upS[1]);
            } else if (oversampleMode_ == 2) {
                double upM[4], upS[4];
                os4M_.upsample (M, upM); os4S_.upsample (S, upS);
                for (int j = 0; j < 4; ++j) eqPass (upM[j], upS[j]);
                M = os4M_.downsample (upM[0], upM[1], upM[2], upM[3]);
                S = os4S_.downsample (upS[0], upS[1], upS[2], upS[3]);
            } else {
                eqPass (M, S);
            }
            L = M + S;
            R = M - S;
        }

        // Analyzer post-EQ tap (display only): "Output" source shows the EQ's
        // effect (Pro-Q's pre/post EQ analyzer). Mid = (L+R)/2.
        if ((int)analyzerScratchOut_.size() > i)
            analyzerScratchOut_[(size_t)i] = 0.5 * (L + R);

        if (invert) {
            L = -L; R = -R;
        }

        // Dynamics: broadband / mid-side / spectral.
        if (compOn) {
            if (spectralActive_) {
                spectralL_.process (&L, 1);
                spectralR_.process (&R, 1);
            } else if (msActive_) {
                // Mid/Side compression: encode, compress M and S independently
                // (each with the shared engine + lookahead), decode. Lets you
                // glue the mid and tame a wide side independently — the standard
                // mastering M/S move.
                const double mid  = 0.5 * (L + R);
                const double side = 0.5 * (L - R);
                const double grM = compressor_.processGrDb (mid);
                const double grS = compressorSide_.processGrDb (side);
                const double gainM = dbToLin (-grM + (autoMk ? compMk + compressor_.currentGrDb() : compMk));
                const double gainS = dbToLin (-grS + (autoMk ? compMk + compressorSide_.currentGrDb() : compMk));
                const double Mc = compDelayM_.process (mid)  * gainM;
                const double Sc = compDelayS_.process (side) * gainS;
                L = Mc + Sc;
                R = Mc - Sc;
            } else {
                // Lookahead: the sidechain (undelayed) drives the GR, which is
                // applied to the DELAYED audio — so the attack is fully ramped
                // before the transient arrives. (A delayed GR on undelayed audio
                // would lag the gain; this is the correct direction.)
                const double sc = 0.5 * (L + R);
                const double gr = compressor_.processGrDb (sc);
                if (autoMk) makeupDb = compMk + compressor_.currentGrDb();
                const double gain = dbToLin (-gr + makeupDb);
                L = compDelayL_.process (L) * gain;
                R = compDelayR_.process (R) * gain;
            }
        }

        // Flush Match (auto gain).
        flushMatch_.pushInput (inL * inGainLin, inR * inGainLin);
        flushMatch_.pushOutput (L, R);
        const double flushGain = flushMatch_.tickGain();
        L *= flushGain; R *= flushGain;

        // Output trim.
        const double outGainLin = outputGainSmooth_.update (targetOutGain);
        L *= outGainLin; R *= outGainLin;

        // Output metering (true-peak — the DAC/limiter sees intersample peaks).
        outPeak = std::max (outPeak, std::max (tpOutL_.process (L), tpOutR_.process (R)));
        outSumSq += L * L + R * R;

        // Write out. Mono source -> duplicate L to both output channels; stereo
        // source -> L/R. (r aliases l when the output is mono, which is correct.)
        l[i] = (float)L;
        if (stereoOut) r[i] = (float)(stereoIn ? R : L);
    }

    const double n = buffer.getNumSamples();
    const double rmsScale = 1.0 / (2.0 * n);

    meterInPeak.store  ((float)linToDb (inPeak));
    meterInRms.store   ((float)linToDb (std::sqrt (inSumSq * rmsScale)));
    meterOutPeak.store ((float)linToDb (outPeak));
    meterOutRms.store  ((float)linToDb (std::sqrt (outSumSq * rmsScale)));

    // Gain reduction: spectral peak-bin, mid/side (max of M and S), or broadband.
    const float grDb = spectralActive_
        ? (float)std::max (spectralL_.peakGrDb(), spectralR_.peakGrDb())
        : msActive_
            ? (float)std::max (compressor_.currentGrDb(), compressorSide_.currentGrDb())
            : (float)compressor_.currentGrDb();
    meterGrDb.store (grDb);

    meterFlushDb.store ((float)flushMatch_.deltaDb());

    const double midRms  = std::sqrt (midSumSq / n);
    const double sideRms = std::sqrt (sideSumSq / n);
    meterMidDb.store  ((float)linToDb (midRms));
    meterSideDb.store ((float)linToDb (sideRms));
    const double denom = (midSumSq + sideSumSq);
    meterCorr.store (denom > 1e-12 ? (float)(midSideDot / denom) : 0.0f);

    // Feed the analyzer with this block's mono mix (display only) — input or
    // post-EQ output, per the "Analyzer Source" setting.
    if (paramB ("analyzer_on", true)) {
        const bool postEq = (paramI ("analyzer_source") == 1);
        analyzer_.process (postEq ? analyzerScratchOut_.data() : analyzerScratch_.data(),
                           buffer.getNumSamples());
    }
}

//==============================================================================
juce::AudioProcessorEditor* FlushAudioProcessor::createEditor()
{
    return new FlushAudioProcessorEditor (*this);
}

bool FlushAudioProcessor::hasEditor() const { return true; }

const juce::String FlushAudioProcessor::getName() const { return "Flush"; }
bool FlushAudioProcessor::acceptsMidi() const { return false; }
bool FlushAudioProcessor::producesMidi() const { return false; }
bool FlushAudioProcessor::isMidiEffect() const { return false; }
double FlushAudioProcessor::getTailLengthSeconds() const { return 0.0; }

//==============================================================================
int FlushAudioProcessor::getNumPrograms() { return 1; }
int FlushAudioProcessor::getCurrentProgram() { return 0; }
void FlushAudioProcessor::setCurrentProgram (int) {}
const juce::String FlushAudioProcessor::getProgramName (int) { return {}; }
void FlushAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
std::unique_ptr<juce::XmlElement> FlushAudioProcessor::createStateXml() const
{
    auto state = parameters.copyState();
    {
        std::lock_guard<std::mutex> lock (bandMutex_);
        state.appendChild (bandTree.createCopy(), nullptr);
    }
    return std::unique_ptr<juce::XmlElement> (state.createXml());
}

void FlushAudioProcessor::applyStateXml (const juce::XmlElement& xml)
{
    auto state = juce::ValueTree::fromXml (xml);
    if (! state.isValid()) return;

    {
        std::lock_guard<std::mutex> lock (bandMutex_);
        auto bands = state.getChildWithName ("BANDS");
        if (bands.isValid()) {
            bandTree.copyPropertiesAndChildrenFrom (bands, nullptr);
            state.removeChild (bands, nullptr);
            bandsDirty_.store (true);
        }
        ensureBandCount();
    }
    parameters.replaceState (state);
    linearFirDirty_.store (true);
}

void FlushAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    std::unique_ptr<juce::XmlElement> xml = createStateXml();
    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
}

void FlushAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Hosts pass arbitrary bytes; malformed/corrupt data must never throw into
    // the message thread. Wrap the whole restore defensively.
    try {
        std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
        if (xml == nullptr) return;
        applyStateXml (*xml);
    } catch (...) {
        // Intentionally swallow: a bad preset file must not crash the host.
    }
}

void FlushAudioProcessor::ensureBandCount()
{
    // The audio path assumes exactly kMaxBands children; a truncated state file
    // (or hand-edited preset) could otherwise make getChild(i) invalid and crash
    // the next buildBand(). Pad or trim defensively. Callers hold bandMutex_.
    while (bandTree.getNumChildren() < kMaxBands) {
        juce::ValueTree b ("BAND");
        b.setProperty ("id", bandTree.getNumChildren(), nullptr);
        b.setProperty ("enabled", false, nullptr);
        bandTree.appendChild (b, nullptr);
    }
    while (bandTree.getNumChildren() > kMaxBands)
        bandTree.removeChild (bandTree.getNumChildren() - 1, nullptr);
}

//==============================================================================
juce::ValueTree FlushAudioProcessor::getBandTreeCopy() const
{
    std::lock_guard<std::mutex> lock (bandMutex_);
    return bandTree.createCopy();
}

juce::ValueTree FlushAudioProcessor::getBand (int index) const
{
    // Bounds-guard the UI's band access; an out-of-range index yields an
    // invalid tree (callers handle it) instead of a crash. Returns a COPY so the
    // caller can read it without racing the audio rebuild.
    std::lock_guard<std::mutex> lock (bandMutex_);
    if (index < 0 || index >= bandTree.getNumChildren())
        return {};
    return bandTree.getChild (index).createCopy();
}

int FlushAudioProcessor::numActiveBands() const
{
    std::lock_guard<std::mutex> lock (bandMutex_);
    int n = 0;
    const int count = juce::jmin (kMaxBands, bandTree.getNumChildren());
    for (int i = 0; i < count; ++i)
        if ((bool)bandTree.getChild (i).getProperty ("enabled", false)) ++n;
    return n;
}

//==============================================================================
// Band editing (UI thread; internally locked)
//==============================================================================
int FlushAudioProcessor::addBand (double freqHz, double gainDb, int shape)
{
    std::lock_guard<std::mutex> lock (bandMutex_);

    // Find the first disabled slot (the 24-band model means "adding" = enabling
    // a slot at the clicked position).
    int slot = -1;
    for (int i = 0; i < kMaxBands; ++i)
        if (!(bool)bandTree.getChild (i).getProperty ("enabled", false)) { slot = i; break; }
    if (slot < 0)
        return -1;                                  // all 24 bands in use

    auto b = bandTree.getChild (slot);
    b.setProperty ("shape",   shape,   nullptr);
    b.setProperty ("freq",    juce::jlimit (10.0, 20000.0, freqHz), nullptr);
    b.setProperty ("gain",    juce::jlimit (-30.0, 30.0, gainDb), nullptr);
    b.setProperty ("q",       1.0,     nullptr);
    b.setProperty ("slope",   12.0,    nullptr);
    b.setProperty ("dynamic", false,   nullptr);
    b.setProperty ("solo",    false,   nullptr);
    b.setProperty ("channel", 0,       nullptr);
    b.setProperty ("enabled", true,    nullptr);

    markBandsDirty();
    return slot;
}

void FlushAudioProcessor::removeBand (int index)
{
    if (index < 0 || index >= kMaxBands) return;
    std::lock_guard<std::mutex> lock (bandMutex_);
    bandTree.getChild (index).setProperty ("enabled", false, nullptr);
    markBandsDirty();
}

void FlushAudioProcessor::moveBand (int index, double freqHz, double gainDb)
{
    if (index < 0 || index >= kMaxBands) return;
    std::lock_guard<std::mutex> lock (bandMutex_);
    auto b = bandTree.getChild (index);
    b.setProperty ("freq", juce::jlimit (10.0, 20000.0, freqHz), nullptr);
    b.setProperty ("gain", juce::jlimit (-30.0, 30.0, gainDb), nullptr);
    markBandsDirty();
}

void FlushAudioProcessor::setBandFreq (int index, double freqHz)
{
    if (index < 0 || index >= kMaxBands) return;
    std::lock_guard<std::mutex> lock (bandMutex_);
    bandTree.getChild (index).setProperty ("freq", juce::jlimit (10.0, 20000.0, freqHz), nullptr);
    markBandsDirty();
}

void FlushAudioProcessor::setBandGain (int index, double gainDb)
{
    if (index < 0 || index >= kMaxBands) return;
    std::lock_guard<std::mutex> lock (bandMutex_);
    bandTree.getChild (index).setProperty ("gain", juce::jlimit (-30.0, 30.0, gainDb), nullptr);
    markBandsDirty();
}

void FlushAudioProcessor::setBandQ (int index, double q)
{
    if (index < 0 || index >= kMaxBands) return;
    std::lock_guard<std::mutex> lock (bandMutex_);
    bandTree.getChild (index).setProperty ("q", juce::jlimit (0.05, 40.0, q), nullptr);
    markBandsDirty();
}

void FlushAudioProcessor::setBandType (int index, int shape)
{
    if (index < 0 || index >= kMaxBands) return;
    std::lock_guard<std::mutex> lock (bandMutex_);
    bandTree.getChild (index).setProperty ("shape", shape, nullptr);
    markBandsDirty();
}

void FlushAudioProcessor::setBandChannel (int index, int channel)
{
    if (index < 0 || index >= kMaxBands) return;
    std::lock_guard<std::mutex> lock (bandMutex_);
    bandTree.getChild (index).setProperty ("channel", juce::jlimit (0, 2, channel), nullptr);
    markBandsDirty();
}

void FlushAudioProcessor::setBandDynamic (int index, bool on)
{
    if (index < 0 || index >= kMaxBands) return;
    std::lock_guard<std::mutex> lock (bandMutex_);
    bandTree.getChild (index).setProperty ("dynamic", on, nullptr);
    markBandsDirty();
}

void FlushAudioProcessor::setBandSolo (int index, bool on)
{
    if (index < 0 || index >= kMaxBands) return;
    std::lock_guard<std::mutex> lock (bandMutex_);
    bandTree.getChild (index).setProperty ("solo", on, nullptr);
    markBandsDirty();
}

void FlushAudioProcessor::setBandSlope (int index, double slopeDbOct)
{
    if (index < 0 || index >= kMaxBands) return;
    std::lock_guard<std::mutex> lock (bandMutex_);
    // The DSP quantizes slope to 12/24/36/48 dB/oct (1-4 2nd-order sections).
    bandTree.getChild (index).setProperty ("slope", juce::jlimit (12.0, 48.0, slopeDbOct), nullptr);
    markBandsDirty();
}

void FlushAudioProcessor::setBandDynRange (int index, double rangeDb)
{
    if (index < 0 || index >= kMaxBands) return;
    std::lock_guard<std::mutex> lock (bandMutex_);
    bandTree.getChild (index).setProperty ("dynRange", juce::jlimit (0.0, 60.0, rangeDb), nullptr);
    markBandsDirty();
}

void FlushAudioProcessor::setBandGainDynRange (int index, double gainDb, double rangeDb)
{
    if (index < 0 || index >= kMaxBands) return;
    std::lock_guard<std::mutex> lock (bandMutex_);
    auto b = bandTree.getChild (index);
    b.setProperty ("gain", juce::jlimit (-30.0, 30.0, gainDb), nullptr);
    b.setProperty ("dynRange", juce::jlimit (0.0, 60.0, rangeDb), nullptr);
    markBandsDirty();
}

double FlushAudioProcessor::eqResponseDb (double freqHz) const
{
    std::lock_guard<std::mutex> lock (uiMutex_);
    const double w = 2.0 * kPi * freqHz / std::max (1.0, uiProcRate_);
    // PARALLEL topology with Mid/Side: L_out = A*L + B*R where
    // A = (H_mid + H_side)/2. For an L-only (symmetric) signal the L channel
    // experiences |A| — this is the physically correct composite curve (a +6 dB
    // mid band reads +3.5 dB, because the unboosted side halves the contribution).
    double mre = 1.0, mim = 0.0, sre = 1.0, sim = 0.0;
    for (int i = 0; i < kMaxBands; ++i) {
        if (!uiEnabled_[i]) continue;
        flush::Complex h = flush::cascadeResponse (uiCoefs_[i], w);
        const double dRe = h.re - 1.0, dIm = h.im;
        const int ch = uiChannel_[i];
        if (ch == 0) { mre += dRe; mim += dIm; sre += dRe; sim += dIm; }
        else if (ch == 1) { mre += dRe; mim += dIm; }
        else if (ch == 2) { sre += dRe; sim += dIm; }
    }
    // A = (H_mid + H_side) / 2
    const double are = 0.5 * (mre + sre), aim = 0.5 * (mim + sim);
    const double mag = std::hypot (are, aim);
    return (mag > 1e-12) ? 20.0 * std::log10 (mag) : -120.0;
}

//==============================================================================
// Factory presets
//==============================================================================
namespace {

struct PresetBand { int shape; double freq, gain, q, slope; bool enabled, dynamic; double dynThr, dynRange; };
struct PresetDef { const char* name; std::vector<PresetBand> bands; };

const std::vector<PresetDef>& factoryPresets()
{
    static const std::vector<PresetDef> presets = {
        { "Clean Up", {
            { (int)flush::Shape::LowCut,    35.0,   0.0, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      250.0, -3.0, 1.2, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::HighShelf, 10000.0, 1.5, 1.0, 12.0, true,  false, -24, 12 },
        } },
        { "Bass", {
            { (int)flush::Shape::LowShelf,  80.0,   3.0, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      400.0, -2.0, 1.5, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::LowCut,    25.0,   0.0, 1.0, 24.0, true,  false, -24, 12 },
        } },
        { "Drums", {
            { (int)flush::Shape::LowCut,    30.0,   0.0, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      120.0,  2.0, 0.8, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      4000.0, -2.0, 1.2, 12.0, true,  true,  -18, 8 },
            { (int)flush::Shape::HighShelf, 8000.0, 2.0, 1.0, 12.0, true,  false, -24, 12 },
        } },
        { "Vocal Presence", {
            { (int)flush::Shape::LowCut,    90.0,   0.0, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      3200.0, 2.5, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      6000.0, -3.0, 4.0, 12.0, true,  true,  -22, 10 },
            { (int)flush::Shape::HighShelf, 12000.0, 1.0, 1.0, 12.0, true,  false, -24, 12 },
        } },
        { "Rap RnB 808", {
            { (int)flush::Shape::LowShelf,  60.0,   2.0, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      200.0, -3.0, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      2500.0, 1.5, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::LowCut,    20.0,   0.0, 1.0, 24.0, true,  false, -24, 12 },
        } },
        { "Mastering Glue", {
            { (int)flush::Shape::HighShelf, 12000.0, 0.5, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      120.0, -0.5, 0.7, 12.0, true,  false, -24, 12 },
        } },
        { "FX Wash", {
            { (int)flush::Shape::Bell,      800.0,  4.0, 0.5, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::BandPass,  2000.0, 0.0, 2.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::HighCut,   8000.0, 0.0, 1.0, 24.0, true,  false, -24, 12 },
        } },
        { "808 Clean", {
            { (int)flush::Shape::LowShelf,  60.0,   2.5, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      250.0, -2.0, 1.2, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::LowCut,    28.0,   0.0, 1.0, 24.0, true,  false, -24, 12 },
        } },
        { "Drill Vocal", {
            { (int)flush::Shape::LowCut,    120.0,  0.0, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      500.0, -2.5, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      2500.0, 3.0, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      5000.0, -4.0, 3.0, 12.0, true,  true,  -20, 8 },
            { (int)flush::Shape::HighShelf, 10000.0, 1.5, 1.0, 12.0, true,  false, -24, 12 },
        } },
        { "RnB Air", {
            { (int)flush::Shape::LowCut,    80.0,   0.0, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      300.0, -1.5, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      3500.0, 2.0, 0.8, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::HighShelf, 12000.0, 2.5, 1.0, 12.0, true,  false, -24, 12 },
        } },
        { "Sub Tighten", {
            { (int)flush::Shape::LowCut,    32.0,   0.0, 1.0, 48.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      180.0, -3.0, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      400.0, -1.5, 1.5, 12.0, true,  false, -24, 12 },
        } },
        { "Punch Bus", {
            { (int)flush::Shape::LowCut,    25.0,   0.0, 1.0, 24.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      60.0,   3.0, 0.7, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      250.0, -2.0, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      3000.0, 2.0, 0.8, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::HighShelf, 9000.0, 1.0, 1.0, 12.0, true,  false, -24, 12 },
        } },
        { "Clarity", {
            { (int)flush::Shape::LowCut,    40.0,   0.0, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      400.0, -2.0, 1.2, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      5000.0, 2.5, 1.2, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::HighShelf, 14000.0, 1.5, 1.0, 12.0, true,  false, -24, 12 },
        } },
        { "Master Warm", {
            { (int)flush::Shape::LowShelf,  80.0,   1.0, 1.0, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::Bell,      250.0, -1.0, 0.7, 12.0, true,  false, -24, 12 },
            { (int)flush::Shape::HighShelf, 10000.0, 1.0, 1.0, 12.0, true,  false, -24, 12 },
        } },
    };
    return presets;
}

} // namespace

juce::StringArray FlushAudioProcessor::getFactoryPresetNames() const
{
    juce::StringArray names;
    for (const auto& p : factoryPresets())
        names.add (p.name);
    return names;
}

void FlushAudioProcessor::loadFactoryPreset (int index)
{
    const auto& presets = factoryPresets();
    if (index < 0 || index >= (int)presets.size())
        return;

    std::lock_guard<std::mutex> lock (bandMutex_);

    // Clear all bands.
    for (int i = 0; i < kMaxBands; ++i) {
        auto b = bandTree.getChild (i);
        b.setProperty ("enabled", false, nullptr);
        b.setProperty ("dynamic", false, nullptr);
        b.setProperty ("solo", false, nullptr);
    }

    // Apply the preset's bands to the first slots.
    const auto& def = presets[index];
    for (int i = 0; i < (int)def.bands.size() && i < kMaxBands; ++i) {
        auto b = bandTree.getChild (i);
        const auto& pb = def.bands[i];
        b.setProperty ("shape",   pb.shape,   nullptr);
        b.setProperty ("freq",    pb.freq,    nullptr);
        b.setProperty ("gain",    pb.gain,    nullptr);
        b.setProperty ("q",       pb.q,       nullptr);
        b.setProperty ("slope",   pb.slope,   nullptr);
        b.setProperty ("enabled", pb.enabled, nullptr);
        b.setProperty ("dynamic", pb.dynamic, nullptr);
        if (pb.dynamic) {
            b.setProperty ("dynThr",   pb.dynThr,   nullptr);
            b.setProperty ("dynRange", pb.dynRange, nullptr);
        }
    }

    markBandsDirty();
}

juce::File FlushAudioProcessor::userPresetDir() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Flush").getChildFile ("Presets");
    if (! dir.exists())
        dir.createDirectory();
    return dir;
}

juce::File FlushAudioProcessor::userPresetFile (const juce::String& name) const
{
    // Sanitize the name for a filename: strip path separators.
    juce::String safe = name.trim();
    if (safe.isEmpty()) safe = "Untitled";
    return userPresetDir().getChildFile (safe + ".xml");
}

juce::StringArray FlushAudioProcessor::getUserPresetNames() const
{
    juce::StringArray names;
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Flush").getChildFile ("Presets");
    if (! dir.exists()) return names;
    for (auto it = juce::RangedDirectoryIterator (dir, false, "*.xml"); it.next();)
        names.add (it.getFile().getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

juce::StringArray FlushAudioProcessor::getAllPresetNames() const
{
    auto names = getFactoryPresetNames();
    names.addArray (getUserPresetNames());
    return names;
}

void FlushAudioProcessor::saveUserPreset (const juce::String& name)
{
    std::unique_ptr<juce::XmlElement> xml = createStateXml();
    if (xml == nullptr) return;
    auto f = userPresetFile (name);
    f.getParentDirectory().createDirectory();
    if (! xml->writeTo (f))
        jassertfalse;                       // write failed (permissions?)
}

void FlushAudioProcessor::loadUserPreset (const juce::String& name)
{
    auto f = userPresetFile (name);
    if (! f.existsAsFile()) return;
    try {
        auto xml = juce::parseXML (f);
        if (xml != nullptr)
            applyStateXml (*xml);
    } catch (...) {
        // A corrupt preset file must not crash the host.
    }
}

void FlushAudioProcessor::loadPresetByName (const juce::String& name)
{
    const auto factory = getFactoryPresetNames();
    const int idx = factory.indexOf (name);
    if (idx >= 0)
        loadFactoryPreset (idx);
    else
        loadUserPreset (name);
}

bool FlushAudioProcessor::deleteUserPreset (const juce::String& name)
{
    auto f = userPresetFile (name);
    return f.existsAsFile() ? f.deleteFile() : false;
}

void FlushAudioProcessor::storeAbSlot (int slot)
{
    slot = std::max (0, std::min (1, slot));
    auto state = parameters.copyState();
    {
        std::lock_guard<std::mutex> lock (bandMutex_);
        state.appendChild (bandTree.createCopy(), nullptr);
    }
    abSlots[slot] = state;
}

void FlushAudioProcessor::recallAbSlot (int slot)
{
    slot = std::max (0, std::min (1, slot));
    if (!abSlots[slot].isValid())
        return;

    auto state = abSlots[slot].createCopy();
    {
        std::lock_guard<std::mutex> lock (bandMutex_);
        auto bands = state.getChildWithName ("BANDS");
        if (bands.isValid()) {
            bandTree.copyPropertiesAndChildrenFrom (bands, nullptr);
            state.removeChild (bands, nullptr);
        }
        ensureBandCount();
    }
    parameters.replaceState (state);
    markBandsDirty();
}
