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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlushAudioProcessorEditor)
};
