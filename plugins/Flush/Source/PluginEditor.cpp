#include "PluginProcessor.h"
#include "PluginEditor.h"

FlushAudioProcessorEditor::FlushAudioProcessorEditor (FlushAudioProcessor& p)
    : VisagePluginEditor (p), audioProcessor (p)
{
    setSize (1100, 640);
}

FlushAudioProcessorEditor::~FlushAudioProcessorEditor() = default;

void FlushAudioProcessorEditor::onInit()
{
    mainView = std::make_unique<FlushMainView> (audioProcessor);
    setEventRoot (mainView.get());
    addFrameToCanvas (mainView.get());
    mainView->setBounds (0, 0, getWidth(), getHeight());
}

void FlushAudioProcessorEditor::onRender()
{
    // High-refresh repaint (the render loop runs at 120 Hz+; uncapped targets
    // host-limited vsync). Meter/analyzer animations drive redraw here.
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
