/*
  ==============================================================================

    WebView-based plugin editor for MYK Tracker.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      webView {
        juce::WebBrowserComponent::Options{}
            .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
            .withWinWebView2Options(
                juce::WebBrowserComponent::Options::WinWebView2{}
                    .withBackgroundColour(juce::Colours::black)
                    .withUserDataFolder(juce::File::getSpecialLocation(
                        juce::File::SpecialLocationType::tempDirectory)))
    }
{
    setSize (1024, 768);

    addAndMakeVisible(webView);
    webView.setWantsKeyboardFocus(true);
    webView.goToURL("http://127.0.0.1:8080/index.html");
}

PluginEditor::~PluginEditor() = default;

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void PluginEditor::resized()
{
    webView.setBounds(getLocalBounds());
}
