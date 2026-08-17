#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "VisageControls.h"
#include "VisageJuceHost.h"

//==============================================================================
class FlushAudioProcessorEditor : public VisagePluginEditor
{
public:
    explicit FlushAudioProcessorEditor (FlushAudioProcessor&);
    ~FlushAudioProcessorEditor() override;

    void onInit() override;
    void onRender() override;
    void onDestroy() override;
    void onResize (int w, int h) override;

private:
    FlushAudioProcessor& audioProcessor;
    std::unique_ptr<FlushMainView> mainView;

    // UI scaling / refresh (Pro-Q settings modal drives these):
    //   ui_scale 0.5..2.0 grows the window (and, proportionally, the controls);
    //   fps_mode 60 / 120 / Uncapped changes the repaint timer rate.
    int   baseWidth_  = 1100;
    int   baseHeight_ = 640;
    float lastScale_  = -1.0f;
    int   lastFps_    = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlushAudioProcessorEditor)
};
