/*
  ==============================================================================

    WebView-based plugin editor for MYK Tracker.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

class PluginEditor::StateWebView : public juce::WebBrowserComponent
{
public:
    StateWebView(PluginEditor& ownerIn, juce::WebBrowserComponent::Options options)
        : juce::WebBrowserComponent(std::move(options)), owner(ownerIn)
    {
    }

    void pageFinishedLoading(const juce::String& url) override
    {
        owner.handlePageFinishedLoading(url);
    }

private:
    PluginEditor& owner;
};

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p)
{
    juce::WebBrowserComponent::Options options {};
    options = options
                  .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
                  .withWinWebView2Options(
                      juce::WebBrowserComponent::Options::WinWebView2{}
                          .withBackgroundColour(juce::Colours::black)
                          .withUserDataFolder(juce::File::getSpecialLocation(
                              juce::File::SpecialLocationType::tempDirectory)));

    webView = std::make_unique<StateWebView>(*this, options);
    setSize (1024, 768);

    addAndMakeVisible(*webView);
    webView->setWantsKeyboardFocus(true);
    webView->goToURL("http://127.0.0.1:8080/index.html");
    audioProcessor.addChangeListener(this);
}

PluginEditor::~PluginEditor()
{
    audioProcessor.removeChangeListener(this);
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void PluginEditor::resized()
{
    if (webView)
        webView->setBounds(getLocalBounds());
}

void PluginEditor::initialiseWebBridge()
{
    // Bootstrap a queue so we can deliver state even if handleNativeState isn't defined yet.
    const auto script = juce::String(
        "(function(){"
        " const q=[];"
        " const flush=()=>{"
        "   if (typeof window.handleNativeState !== 'function') return;"
        "   while(q.length){"
        "     const s=q.shift();"
        "     try { window.handleNativeState(s); } catch(e) { console.error('handleNativeState error', e); }"
        "     if (window.__JUCE__ && typeof window.__JUCE__.emitResult==='function') window.__JUCE__.emitResult('ok');"
        "   }"
        " };"
        " window.__MYK__ = window.__MYK__ || {};"
        " window.__MYK__.deliver = (state)=>{ q.push(state); flush(); setTimeout(flush,0); };"
        " document.addEventListener('DOMContentLoaded', flush);"
        "})();");

    if (webView)
        webView->evaluateJavascript(script, [] (juce::WebBrowserComponent::EvaluationResult) {});
}

void PluginEditor::pushStateToWebView(const juce::String& stateJson)
{
    if (!isVisible() || webView == nullptr)
        return;

    if (!pageReady)
    {
        pendingStateJson = stateJson;
        return;
    }

    if (stateJson.isEmpty())
        return;

    // Deliver via the bootstrap queue to avoid missing handlers; include a callback to satisfy JUCE.
    const juce::String script = "window.__MYK__ && window.__MYK__.deliver(" + stateJson + ");";
    const auto start = juce::Time::getMillisecondCounterHiRes();
    webView->evaluateJavascript(
        script,
        [start] (juce::WebBrowserComponent::EvaluationResult) {
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
            DBG("handleNativeState eval+callback took " << juce::String(elapsed, 2) << " ms");
        });
}

void PluginEditor::handlePageFinishedLoading(const juce::String& url)
{
    DBG( "PluginEditor::handlePageFinishedLoading loaded url " << url );
    if (url == "about:blank") { return; }
    juce::ignoreUnused(url);
    pageReady = true;
    initialiseWebBridge();

    if (pendingStateJson.isNotEmpty())
    {
        auto stateCopy = pendingStateJson;
        pendingStateJson.clear();
        pushStateToWebView(stateCopy);
    }
}

void PluginEditor::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);
    juce::String json;
    if (!audioProcessor.tryGetLatestSerializedUiState(json))
        return;

    if (!pageReady)
    {
        pendingStateJson = json;
        return;
    }

    pushStateToWebView(json);
}
