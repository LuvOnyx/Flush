#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {

// ------------------------------------------------------------- tiny helpers --
constexpr double kPi = 3.14159265358979323846;

inline double dbToLin (double db)  { return std::pow (10.0, db / 20.0); }
inline double linToDb (double lin) { return (lin > 1e-12) ? 20.0 * std::log10 (lin) : -120.0; }

// Butterworth section Q for each 12 dB/oct of a cut filter.
constexpr double kCutQ = 0.7071067811865476;

inline juce::String floatToStringHz (float value, int) {
    if (value >= 1000.0f) return juce::String (value / 1000.0f, 2) + " kHz";
    return juce::String (value, 0) + " Hz";
}

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

    bandsDirty_ = true;
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
        juce::StringArray { "Broadband", "Spectral" }, 0));
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

    // Oversampling doubles the internal processing rate (EQ section only).
    oversampleOn_ = ((int)*parameters.getRawParameterValue ("eq_oversample") > 0);
    lastOversample_ = oversampleOn_ ? 1 : 0;
    procRate_ = oversampleOn_ ? sampleRate * 2.0 : sampleRate;

    inputGainSmooth_.setTau (0.005, sampleRate);
    outputGainSmooth_.setTau (0.005, sampleRate);

    compressor_.reset (sampleRate);
    flushMatch_.reset (sampleRate);
    flushMatch_.setTiming ((int)*parameters.getRawParameterValue ("flush_speed"));

    tpInL_.reset (sampleRate); tpInR_.reset (sampleRate);
    tpOutL_.reset (sampleRate); tpOutR_.reset (sampleRate);

    analyzer_.reset (sampleRate);
    analyzer_.setSpeed ((int)*parameters.getRawParameterValue ("analyzer_speed"));
    analyzer_.setTilt ((double)*parameters.getRawParameterValue ("analyzer_tilt"));
    analyzer_.setRange ({ 60.0, 90.0, 120.0 }[(int)*parameters.getRawParameterValue ("analyzer_range")]);
    analyzerScratch_.resize (samplesPerBlock);

    osM_.reset (sampleRate);
    osS_.reset (sampleRate);
    os4M_.reset (sampleRate);
    os4S_.reset (sampleRate);

    spectralL_.reset (sampleRate, 2048);
    spectralR_.reset (sampleRate, 2048);

    linearFirDirty_ = true;
    refreshBands (procRate_);
    updateLatency();
}

void FlushAudioProcessor::releaseResources()
{
    for (auto& b : bands_) {
        b.sectionsM.clear();
        b.sectionsS.clear();
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
    return (int)*parameters.getRawParameterValue ("eq_phase_mode");
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
    if (*parameters.getRawParameterValue ("eq_gain_q_link") > 0.5f)
        br.q = flush::gainQLinkedQ (br.q, br.gainDb);
    br.dynamic     = (bool)b.getProperty ("dynamic", false);
    br.dynThresholdDb = (double)b.getProperty ("dynThr", -24.0);
    br.dynRangeDb  = (double)b.getProperty ("dynRange", 12.0);
    br.dynAttackSec = (double)b.getProperty ("dynAtk", 0.010);
    br.dynReleaseSec = (double)b.getProperty ("dynRel", 0.150);
    br.channel     = (int)b.getProperty ("channel", 0);
    br.solo        = (bool)b.getProperty ("solo", false);

    br.sectionsM.clear();
    br.sectionsS.clear();
    br.coefs.clear();

    const double fs = sampleRate, f0 = br.freq, gain = br.gainDb, Q = br.q;

    // Phase-mode routing (no-cramp matched/decramped designs only — RBJ is a
    // measurement baseline, never a user-facing path):
    //   Low Latency -> Vicanek matched (2016)
    //   Natural     -> Orfanidis decramped (the Pro-Q "high-freq decramping")
    //   Linear      -> matched for now; FIR path is the next implementation pass
    const bool orfanidisMode = natural;

    auto pushSection = [&](flush::BiquadCoef c) {
        flush::MorphingBiquad bm; bm.setCoeffs (c, 0);   // ramp 0 = instant on build
        flush::MorphingBiquad bs; bs.setCoeffs (c, 0);
        br.sectionsM.push_back (bm);
        br.sectionsS.push_back (bs);
        br.coefs.push_back (c);                          // for linear-phase FIR design
    };

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
            // Slope 12-48 dB/oct in 12 dB steps => 1-4 cascaded 2nd-order sections.
            const int n = std::max (1, std::min (4, (int)std::lround (br.slopeDbOct / 12.0)));
            for (int i = 0; i < n; ++i)
                pushSection (flush::matched::lowpass (fs, f0, kCutQ));
            break;
        }

        case flush::Shape::HighCut: {
            const int n = std::max (1, std::min (4, (int)std::lround (br.slopeDbOct / 12.0)));
            for (int i = 0; i < n; ++i)
                pushSection (flush::matched::highpass (fs, f0, kCutQ));
            break;
        }

        case flush::Shape::BandPass:
            pushSection (flush::matched::bandpass (fs, f0, Q));
            break;

        case flush::Shape::Notch:
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
        br.dyn.setParams (p);
    }
}

void FlushAudioProcessor::refreshBands (double sampleRate)
{
    const bool natural = (phaseMode() == 1);
    for (int i = 0; i < kMaxBands; ++i)
        buildBand (i, bandTree.getChild (i), sampleRate, natural);
    bandsDirty_ = false;
}

// Process one channel's cascade, returning the filtered sample.
double FlushAudioProcessor::processBandChannel (std::vector<flush::MorphingBiquad>& sections, double in)
{
    double out = in;
    for (auto& s : sections)
        out = s.process (out);
    return out;
}

// Total magnitude of a band's cascade at frequency f (Hz) — used to design the
// combined linear-phase FIR of the whole static EQ.
double FlushAudioProcessor::bandMagnitudeAt (int index, double f) const
{
    const auto& br = bands_[index];
    const double w = 2.0 * kPi * f / procRate_;
    double mag = 1.0;
    for (const auto& c : br.coefs)
        mag *= flush::magnitude (c, w);
    return mag;
}

// Rebuild the combined linear-phase FIR from the static (non-dynamic) bands.
void FlushAudioProcessor::rebuildLinearFir()
{
    linearReady_ = false;

    auto magFn = [&](double f) {
        double m = 1.0;
        for (int i = 0; i < kMaxBands; ++i) {
            if (!bands_[i].enabled || bands_[i].dynamic) continue;   // static only
            m *= bandMagnitudeAt (i, f);
        }
        return m;
    };

    linearCoeffs_ = flush::designLinearPhaseFir (magFn, procRate_, firLength_, 6.0);
    linearFirM_.set (linearCoeffs_);
    linearFirS_.set (linearCoeffs_);
    linearFirDirty_ = false;
    linearReady_ = true;
}

void FlushAudioProcessor::processEqSample (double& m, double& s)
{
    for (int i = 0; i < kMaxBands; ++i) {
        const auto& br = bands_[i];
        if (!br.enabled) continue;

        // In linear-phase mode the static bands are handled by the combined FIR;
        // only the (minimum-phase) dynamic bands pass through this loop.
        if (linearActive_ && !br.dynamic) continue;

        const bool useM = (br.channel == 0 || br.channel == 1);   // stereo | mid
        const bool useS = (br.channel == 0 || br.channel == 2);   // stereo | side

        double bandM = useM ? processBandChannel (br.sectionsM, m) : m;
        double bandS = useS ? processBandChannel (br.sectionsS, s) : s;

        double k = 1.0;
        if (br.dynamic) {
            // Sidechain = band-limited signal at full static gain (no feedback).
            double sc = (useM && useS) ? 0.5 * (bandM + bandS)
                       : useM         ? bandM
                                      : bandS;
            const double gr = br.dyn.processGrDb (sc);
            k = dbToLin (-gr);
        }

        if (useM) m = m + k * (bandM - m);
        if (useS) s = s + k * (bandS - s);
    }
}

//==============================================================================
void FlushAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalIn  = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();

    for (int i = totalIn; i < totalOut; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (buffer.getNumSamples() == 0)
        return;

    // --- parameter snapshot -------------------------------------------------
    const bool   eqOn      = *parameters.getRawParameterValue ("eq_enabled") > 0.5f;
    const bool   compOn    = *parameters.getRawParameterValue ("comp_enabled") > 0.5f;
    const bool   invert    = *parameters.getRawParameterValue ("eq_phase_invert") > 0.5f;
    const float  inGainDb  = *parameters.getRawParameterValue ("input_gain");
    const float  outGainDb = *parameters.getRawParameterValue ("output_gain");
    const int    compDet   = (int)*parameters.getRawParameterValue ("comp_detector");
    const bool   autoMk    = *parameters.getRawParameterValue ("comp_auto_makeup") > 0.5f;
    const float  compMk    = *parameters.getRawParameterValue ("comp_makeup");
    const int    flushMode = (int)*parameters.getRawParameterValue ("flush_mode");
    const int    flushRef  = (int)*parameters.getRawParameterValue ("flush_reference");
    const int    flushSpd  = (int)*parameters.getRawParameterValue ("flush_speed");
    const float  targetDb  = *parameters.getRawParameterValue ("match_target");

    // --- phase mode / oversampling / band rebuild ---------------------------
    linearActive_ = (phaseMode() == 2);
    const int oversampleNow = (int)*parameters.getRawParameterValue ("eq_oversample");  // 0/1/2
    if (oversampleNow != oversampleMode_ || phaseMode() != lastPhaseMode_) {
        oversampleMode_ = oversampleNow;
        procRate_ = sampleRate_ * (1 << oversampleMode_);
        osM_.reset (sampleRate_); osS_.reset (sampleRate_);
        os4M_.reset (sampleRate_); os4S_.reset (sampleRate_);
        lastPhaseMode_ = phaseMode();
        bandsDirty_ = true;
        linearFirDirty_ = true;
    }
    if (bandsDirty_)
        refreshBands (procRate_);
    if (linearActive_) {
        if (linearFirDirty_)
            rebuildLinearFir();
    } else {
        linearReady_ = false;
    }
    updateLatency();

    // --- analyzer configuration (display only, runtime-changeable) ----------
    if (*parameters.getRawParameterValue ("analyzer_on") > 0.5f) {
        analyzer_.setSpeed ((int)*parameters.getRawParameterValue ("analyzer_speed"));
        analyzer_.setTilt ((double)*parameters.getRawParameterValue ("analyzer_tilt"));
        analyzer_.setFreeze (*parameters.getRawParameterValue ("analyzer_freeze") > 0.5f);
    }

    // --- compressor / spectral dynamics (shared threshold/ratio/attack/release) ---
    const bool spectralMode = (int)*parameters.getRawParameterValue ("comp_mode") == 1;
    spectralActive_ = compOn && spectralMode;

    if (spectralActive_) {
        flush::SpectralParams sp;
        sp.thresholdDb = *parameters.getRawParameterValue ("comp_threshold");
        sp.ratio       = *parameters.getRawParameterValue ("comp_ratio");
        sp.rangeDb     = 24.0;
        sp.attackSec   = *parameters.getRawParameterValue ("comp_attack") / 1000.0;
        sp.releaseSec  = *parameters.getRawParameterValue ("comp_release") / 1000.0;
        sp.kneeDb      = 6.0;
        spectralL_.setParams (sp);
        spectralR_.setParams (sp);
    } else {
        flush::DynamicsParams p;
        p.thresholdDb = *parameters.getRawParameterValue ("comp_threshold");
        p.ratio       = *parameters.getRawParameterValue ("comp_ratio");
        p.attackSec   = *parameters.getRawParameterValue ("comp_attack") / 1000.0;
        p.releaseSec  = *parameters.getRawParameterValue ("comp_release") / 1000.0;
        p.kneeDb      = 6.0;
        p.rangeDb     = 60.0;
        p.rms         = (compDet == 1);
        p.adaptiveRelease = true;   // transparent: heavy GR hangs, light GR lets go
        p.adaptiveAmount  = 4.0;
        compressor_.setParams (p);
    }

    // --- Flush Match configuration -----------------------------------------
    flushMatch_.setMode ((flush::FlushMatch::Mode)flushMode);
    flushMatch_.setReference ((flush::FlushMatch::Reference)flushRef);
    flushMatch_.setTargetDb (targetDb);
    flushMatch_.setTiming (flushSpd);

    // --- process ------------------------------------------------------------
    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (std::min (1, totalIn - 1));

    const double targetInGain  = dbToLin (inGainDb);
    const double targetOutGain = dbToLin (outGainDb);

    double inPeak = 0.0, inSumSq = 0.0, outPeak = 0.0, outSumSq = 0.0;
    double midSumSq = 0.0, sideSumSq = 0.0, midSideDot = 0.0;
    double makeupDb = compMk;

    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        const double inL = (totalIn > 0) ? l[i] : 0.0;
        const double inR = (totalIn > 1) ? r[i] : inL;   // mono -> duplicate

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

        if (invert) {
            L = -L; R = -R;
        }

        // Dynamics: broadband compressor OR spectral dynamics (per-frequency).
        if (compOn) {
            if (spectralActive_) {
                spectralL_.process (&L, 1);
                spectralR_.process (&R, 1);
            } else {
                const double sc = 0.5 * (L + R);
                const double gr = compressor_.processGrDb (sc);
                if (autoMk) makeupDb = compMk + compressor_.currentGrDb();
                const double gain = dbToLin (-gr + makeupDb);
                L *= gain; R *= gain;
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

        l[i] = (float)L;
        if (totalOut > 1 && totalIn > 1) r[i] = (float)R;
    }

    const double n = buffer.getNumSamples();
    const double rmsScale = 1.0 / (2.0 * n);

    meterInPeak.store  ((float)linToDb (inPeak));
    meterInRms.store   ((float)linToDb (std::sqrt (inSumSq * rmsScale)));
    meterOutPeak.store ((float)linToDb (outPeak));
    meterOutRms.store  ((float)linToDb (std::sqrt (outSumSq * rmsScale)));
    meterGrDb.store    ((float)compressor_.currentGrDb());
    meterFlushDb.store ((float)flushMatch_.deltaDb());

    const double midRms  = std::sqrt (midSumSq / n);
    const double sideRms = std::sqrt (sideSumSq / n);
    meterMidDb.store  ((float)linToDb (midRms));
    meterSideDb.store ((float)linToDb (sideRms));
    const double denom = (midSumSq + sideSumSq);
    meterCorr.store (denom > 1e-12 ? (float)(midSideDot / denom) : 0.0f);

    // Feed the analyzer with this block's mono mix (display only).
    if (*parameters.getRawParameterValue ("analyzer_on") > 0.5f)
        analyzer_.process (analyzerScratch_.data(), buffer.getNumSamples());
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
void FlushAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.appendChild (bandTree.createCopy(), nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void FlushAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr) return;

    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid()) return;

    // Split off the band tree before replacing parameter state.
    auto bands = state.getChildWithName ("BANDS");
    if (bands.isValid()) {
        bandTree.copyPropertiesAndChildrenFrom (bands, nullptr);
        state.removeChild (bands, nullptr);
        bandsDirty_ = true;
    }
    parameters.replaceState (state);
}

//==============================================================================
juce::ValueTree FlushAudioProcessor::getBand (int index) const
{
    return bandTree.getChild (index);
}

int FlushAudioProcessor::numActiveBands() const
{
    int n = 0;
    for (int i = 0; i < kMaxBands; ++i)
        if ((bool)bandTree.getChild (i).getProperty ("enabled", false)) ++n;
    return n;
}

double FlushAudioProcessor::eqResponseDb (double freqHz) const
{
    double mag = 1.0;
    for (int i = 0; i < kMaxBands; ++i)
        if (bands_[i].enabled)
            mag *= bandMagnitudeAt (i, freqHz);
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

    // Clear all bands.
    for (int i = 0; i < kMaxBands; ++i) {
        auto b = bandTree.getChild (i);
        b.setProperty ("enabled", false, nullptr);
        b.setProperty ("dynamic", false, nullptr);
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

void FlushAudioProcessor::storeAbSlot (int slot)
{
    slot = std::max (0, std::min (1, slot));
    auto state = parameters.copyState();
    state.appendChild (bandTree.createCopy(), nullptr);
    abSlots[slot] = state;
}

void FlushAudioProcessor::recallAbSlot (int slot)
{
    slot = std::max (0, std::min (1, slot));
    if (!abSlots[slot].isValid())
        return;

    auto state = abSlots[slot].createCopy();
    auto bands = state.getChildWithName ("BANDS");
    if (bands.isValid()) {
        bandTree.copyPropertiesAndChildrenFrom (bands, nullptr);
        state.removeChild (bands, nullptr);
    }
    parameters.replaceState (state);
    markBandsDirty();
}
