/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class PluginEditor  : public juce::AudioProcessorEditor,
                      public juce::ChangeListener
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void pushStateToWebView(const juce::String& stateJson);
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

private:
    class StateWebView;
    void handlePageFinishedLoading(const juce::String& url);
    void initialiseWebBridge();

    PluginProcessor& audioProcessor;
    std::unique_ptr<StateWebView> webView;
    bool pageReady { false };
    juce::String pendingStateJson;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
