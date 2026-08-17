#include "PluginProcessor.h"
#include "PluginEditor.h"

FlushAudioProcessorEditor::FlushAudioProcessorEditor (FlushAudioProcessor& p)
    : VisagePluginEditor (p), audioProcessor (p)
{
    setResizable (false, false);
    setSize (baseWidth_, baseHeight_);
}

FlushAudioProcessorEditor::~FlushAudioProcessorEditor() = default;

void FlushAudioProcessorEditor::onInit()
{
    mainView = std::make_unique<FlushMainView> (audioProcessor);
    setEventRoot (mainView.get());
    addFrameToCanvas (mainView.get());
    mainView->setBounds (0, 0, getWidth(), getHeight());

    // Apply the initial refresh rate.
    const int fps = (int)*audioProcessor.parameters.getRawParameterValue ("fps_mode");
    const int hz = fps == 0 ? 60 : fps == 1 ? 120 : 240;   // 240 ~ host-limited "uncapped"
    setRefreshHz (hz);
    lastFps_ = fps;
    lastScale_ = *audioProcessor.parameters.getRawParameterValue ("ui_scale");
}

void FlushAudioProcessorEditor::onRender()
{
    // Live UI settings: apply scale (window size) and refresh rate when they
    // change from the settings modal.
    const float scale = *audioProcessor.parameters.getRawParameterValue ("ui_scale");
    if (scale != lastScale_) {
        lastScale_ = scale;
        setSize (juce::roundToInt (baseWidth_ * scale),
                 juce::roundToInt (baseHeight_ * scale));
    }

    const int fps = (int)*audioProcessor.parameters.getRawParameterValue ("fps_mode");
    if (fps != lastFps_) {
        lastFps_ = fps;
        setRefreshHz (fps == 0 ? 60 : fps == 1 ? 120 : 240);
    }

    if (mainView)
        mainView->redraw();
}

void FlushAudioProcessorEditor::onDestroy()
{
    if (mainView) {
        removeFrameFromCanvas (mainView.get());
        mainView.reset();
    }
}

void FlushAudioProcessorEditor::onResize (int w, int h)
{
    if (mainView) {
        mainView->setBounds (0, 0, w, h);
        mainView->redraw();
    }
}
