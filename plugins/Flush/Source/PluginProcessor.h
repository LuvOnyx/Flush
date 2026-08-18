#pragma once

// Flush — a Pro-Q4-class 24-band dynamic EQ + Flush Match gain staging + glue
// compressor. Sound-quality-first DSP core (double precision, no-cramp matched
// filters) lives in Source/dsp/ and is unit-tested standalone; this processor
// wires it into a JUCE plugin.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include <mutex>
#include <vector>

#include "dsp/FlushFilter.h"
#include "dsp/FlushDynamics.h"
#include "dsp/FlushMatch.h"
#include "dsp/FlushLoudness.h"
#include "dsp/FlushFir.h"
#include "dsp/FlushOversample.h"
#include "dsp/FlushAnalyzer.h"
#include "dsp/FlushSpectral.h"

//==============================================================================
class FlushAudioProcessor : public juce::AudioProcessor
{
public:
    FlushAudioProcessor();
    ~FlushAudioProcessor() override;

    //--- AudioProcessor ------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //--- State / parameters --------------------------------------------------
    juce::AudioProcessorValueTreeState parameters;

    static constexpr int kMaxBands = 24;

    //--- Metering (lock-free, read by the UI at 60-120 Hz) -------------------
    std::atomic<float> meterInPeak  { 0.0f };
    std::atomic<float> meterInRms   { 0.0f };
    std::atomic<float> meterOutPeak { 0.0f };
    std::atomic<float> meterOutRms  { 0.0f };
    std::atomic<float> meterGrDb    { 0.0f };
    std::atomic<float> meterFlushDb { 0.0f };
    std::atomic<float> meterMidDb   { 0.0f };
    std::atomic<float> meterSideDb  { 0.0f };
    std::atomic<float> meterCorr    { 0.0f };
    std::atomic<float> meterLatencyMs { 0.0f };

    //--- Band access for the UI ---------------------------------------------
    // bandTree is PRIVATE and guarded by bandMutex_ (the audio thread reads it
    // during a rebuild; the UI writes it via the editing API below). The UI
    // must go through these accessors — never touch bandTree directly.
    juce::ValueTree getBandTreeCopy() const;   // locked deep copy (safe to read/draw)
    juce::ValueTree getBand (int index) const; // locked copy of one band
    int numActiveBands() const;

    //--- Band editing (UI thread). Internally locked with bandMutex_ so these
    //    are safe to call while the audio thread is running. -----------------
    int  addBand (double freqHz, double gainDb, int shape = (int)flush::Shape::Bell);
    void removeBand (int index);
    void moveBand (int index, double freqHz, double gainDb);
    void setBandFreq (int index, double freqHz);
    void setBandGain (int index, double gainDb);
    void setBandQ (int index, double q);
    void setBandType (int index, int shape);
    void setBandDynamic (int index, bool on);
    void setBandSolo (int index, bool on);

    //--- Analyzer access for the UI (lock-free read of the dB magnitudes) ---
    const std::vector<float>& analyzerMagnitudes() { return analyzer_.magnitudes(); }

    //--- EQ composite response (dB) at a frequency — for the UI curve -------
    double eqResponseDb (double freqHz) const;

    //--- Mark the band graph dirty (UI node edits). Lock-free flags so the UI
    //    thread can request a rebuild without touching audio-thread state.
    void markBandsDirty() { bandsDirty_.store (true); linearFirDirty_.store (true); }

    //--- Factory presets ----------------------------------------------------
    juce::StringArray getFactoryPresetNames() const;
    juce::StringArray getUserPresetNames() const;
    juce::StringArray getAllPresetNames() const;   // factory first, then user
    void loadFactoryPreset (int index);
    void loadUserPreset (const juce::String& name);
    void loadPresetByName (const juce::String& name);   // factory or user
    void saveUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);

    //--- A/B compare slots --------------------------------------------------
    void storeAbSlot (int slot);       // 0 = A, 1 = B
    void recallAbSlot (int slot);
    juce::ValueTree abSlots[2];

private:
    // Runtime band (built from bandTree when dirty).
    struct BandRuntime
    {
        bool enabled = true;
        flush::Shape shape = flush::Shape::Bell;
        double freq = 1000.0, gainDb = 0.0, q = 1.0, slopeDbOct = 12.0;
        bool dynamic = false;
        double dynThresholdDb = -24.0, dynRangeDb = 12.0;
        double dynAttackSec = 0.010, dynReleaseSec = 0.150;
        int channel = 0;                 // 0 stereo, 1 mid, 2 side (L/R deferred)
        bool solo = false;

        // Parallel band-split: out = in + k * (bandOut - in), k = dynamic gain.
        // Cuts are multi-section cascades; each channel keeps its own state.
        // Sections morph (output-crossfade) on coefficient change — click-free
        // automation of gain/freq/Q on every band. All shapes (including cuts
        // and band-pass) use the decramped matched designs; `coefs` mirrors the
        // exact magnitude so the FIR designer and UI curve stay in sync.
        std::vector<flush::MorphingBiquad> sectionsM;
        std::vector<flush::MorphingBiquad> sectionsS;
        std::vector<flush::BiquadCoef> coefs;   // per-section coefficients (for FIR design)
        flush::DynamicsEngine dyn;
    };

    void buildBand (int index, const juce::ValueTree& b, double sampleRate, bool natural);
    void refreshBands (double sampleRate);
    void ensureBandCount();               // normalize BANDS child count to kMaxBands
    void publishUiSnapshot (double sampleRate);
    double processBandChannel (std::vector<flush::MorphingBiquad>& sections, double in);
    void processEqSample (double& m, double& s);
    double staticEqMagnitudeLinear (double f) const;   // |1 + Σ(H_i - 1)|, static bands
    void rebuildLinearFir();

    // UI thread reads the EQ curve from a mutex-guarded snapshot of the band
    // coefficients (published after each audio-thread rebuild), never from the
    // live `bands_` buffers — so curve drawing can't race a rebuild.
    mutable std::mutex uiMutex_;
    std::array<std::vector<flush::BiquadCoef>, kMaxBands> uiCoefs_;
    std::array<bool, kMaxBands> uiEnabled_ {};
    double uiProcRate_ = 48000.0;

    int phaseMode() const;               // 0 low-latency, 1 natural, 2 linear
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateLatency();

    // State serialization shared by DAW save/load, user presets, and A/B.
    std::unique_ptr<juce::XmlElement> createStateXml() const;
    void applyStateXml (const juce::XmlElement& xml);
    juce::File userPresetDir() const;
    juce::File userPresetFile (const juce::String& name) const;

    // Null-safe parameter readers. `getRawParameterValue(id)` returns nullptr if
    // the ID is unknown (a typo or a stale state) — dereferencing that is a
    // guaranteed crash. These return the fallback instead, and jassert in debug
    // so the bug is still visible during development.
    float* rawParam (const juce::String& id);
    float  paramF (const juce::String& id, float fallback = 0.0f);
    int    paramI (const juce::String& id, int fallback = 0);
    bool   paramB (const juce::String& id, bool fallback = false);
    void   validateParameterIds();       // jasserts every expected ID exists
    float paramFallback_ = 0.0f;

    std::array<BandRuntime, kMaxBands> bands_;
    std::atomic<bool> bandsDirty_ { true };
    int lastPhaseMode_ = -1;

    // The band source-of-truth (24 x "BAND" children). PRIVATE + guarded by
    // bandMutex_: written by the UI thread (node drags, presets, context menu)
    // and read by the audio thread during a rebuild.
    juce::ValueTree bandTree { "BANDS" };

    // Guards bandTree (the band source-of-truth). It is written by the UI thread
    // (node drags, presets, context menu) and read by the audio thread during a
    // rebuild — without this lock that is a data race on a shared ValueTree.
    std::mutex bandMutex_;

    flush::DynamicsEngine compressor_;
    flush::DynamicsEngine compressorSide_;   // M/S mode: independent Side engine

    // Compressor lookahead: the AUDIO is delayed (L/R, or M/S in M/S mode) so
    // the undelayed gain reduction can be ramped before the transient arrives.
    flush::DelayLine compDelayL_, compDelayR_;
    flush::DelayLine compDelayM_, compDelayS_;

    flush::FlushMatch flushMatch_;

    flush::TruePeakMeter tpInL_, tpInR_, tpOutL_, tpOutR_;

    flush::OnePole inputGainSmooth_;
    flush::OnePole outputGainSmooth_;

    // Spectrum analyzer (observes the input or the post-EQ output; display only).
    flush::Analyzer analyzer_;
    std::vector<double> analyzerScratch_;
    std::vector<double> analyzerScratchOut_;

    // 2x / 4x oversampling of the EQ section (M and S channels).
    flush::Oversampler2x osM_, osS_;
    flush::Oversampler4x os4M_, os4S_;
    int oversampleMode_ = 0;             // 0 off, 1 = 2x, 2 = 4x
    double procRate_ = 48000.0;          // host rate * 2/4 when oversampling

    // Spectral dynamics (compressor mode "Spectral"), stereo.
    flush::SpectralDynamics spectralL_, spectralR_;
    bool spectralActive_ = false;
    bool msActive_ = false;              // compressor mode "Mid/Side"

    // Linear-phase path: one combined FIR for the static EQ (M and S).
    flush::FirFilter linearFirM_, linearFirS_;
    std::vector<double> linearCoeffs_;
    std::atomic<bool> linearFirDirty_ { true };
    bool linearReady_ = false;
    int firLength_ = 2047;               // Medium quality (~21 ms latency @ 48 kHz)
    int lastFirQuality_ = -1;
    bool linearActive_ = false;

    double sampleRate_ = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlushAudioProcessor)
};
