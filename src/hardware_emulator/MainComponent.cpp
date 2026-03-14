#include "MainComponent.h"

namespace
{
constexpr int trackerOscPort = 9001;
constexpr int smallScreenOscPort = 9000;
constexpr const char* trackerOscHost = "127.0.0.1";
constexpr const char* clickAddress = "/click";
constexpr const char* incrementAddress = "/increment";
constexpr const char* decrementAddress = "/decrement";
constexpr const char* zoomInAddress = "/zoom_in";
constexpr const char* zoomOutAddress = "/zoom_out";

juce::String formatOscPayload(const juce::OSCMessage& message)
{
    if (message.size() == 0)
        return {};

    const auto& arg = message[0];
    if (arg.isString())
        return arg.getString();
    if (arg.isInt32())
        return juce::String(arg.getInt32());
    if (arg.isFloat32())
        return juce::String(arg.getFloat32(), 3);
    return {};
}

class OscControlClient
{
public:
    OscControlClient()
    {
        connected = sender.connect(trackerOscHost, trackerOscPort);
    }

    bool isConnected() const
    {
        return connected;
    }

    void sendZoom(const juce::String& address, juce::Point<float> normalizedPoint)
    {
        if (!connected)
            return;

        sender.send(address, normalizedPoint.x, normalizedPoint.y);
    }

    void sendStepDelta(const juce::String& address, int steps)
    {
        if (!connected || steps <= 0)
            return;

        sender.send(address, steps);
    }

    void sendClick()
    {
        if (!connected)
            return;

        sender.send(clickAddress, 1);
    }

private:
    juce::OSCSender sender;
    bool connected = false;
};

class SmallScreenComponent final : public juce::Component,
                                   private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    SmallScreenComponent()
    {
        receiver.addListener(this);
        listening = receiver.connect(smallScreenOscPort);

        if (!listening)
            displayText = "OSC OFFLINE";
    }

    ~SmallScreenComponent() override
    {
        receiver.removeListener(this);
        receiver.disconnect();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        juce::ColourGradient glow(juce::Colour(0xff2a1204), bounds.getCentreX(), bounds.getY(),
                                  juce::Colour(0xff120701), bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(glow);
        g.fillRoundedRectangle(bounds, 10.0f);

        g.setColour(juce::Colour(0xffffa13a));
        g.drawRoundedRectangle(bounds.reduced(2.0f), 10.0f, 2.0f);

        g.setColour(juce::Colour(0xffffcf8b));
        g.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
        g.drawFittedText(displayText, getLocalBounds().reduced(10), juce::Justification::centred, 2);
    }

private:
    void oscMessageReceived(const juce::OSCMessage& message) override
    {
        const auto payload = formatOscPayload(message).trim();
        if (payload.isNotEmpty())
        {
            displayText = payload.substring(0, 32);
            repaint();
        }
    }

    juce::OSCReceiver receiver;
    juce::String displayText { "READY" };
    bool listening = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SmallScreenComponent)
};

class TouchSurfaceComponent final : public juce::Component
{
public:
    explicit TouchSurfaceComponent(OscControlClient& clientToUse)
        : client(clientToUse)
    {
        setWantsKeyboardFocus(false);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        juce::ColourGradient bg(juce::Colour(0xfffff0dc), bounds.getTopLeft(),
                                juce::Colour(0xffffd2a3), bounds.getBottomRight(), false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(bounds, 16.0f);

        g.setColour(juce::Colour(0xff6d3410));
        g.drawRoundedRectangle(bounds.reduced(2.0f), 16.0f, 2.0f);

        g.setFont(juce::Font(juce::FontOptions(28.0f, juce::Font::bold)));
        g.drawText("Touch Surface", 20, 18, getWidth() - 40, 34, juce::Justification::left);

        g.setFont(juce::Font(juce::FontOptions(18.0f)));
        g.drawFittedText("Use trackpad pinch if available.\nMouse wheel also emits zoom in/out around the pointer.",
                         getLocalBounds().reduced(20).withTrimmedTop(56),
                         juce::Justification::topLeft, 4);

        if (lastGestureText.isNotEmpty())
        {
            g.setColour(juce::Colour(0xffb14d0b));
            g.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
            g.drawText(lastGestureText, getLocalBounds().reduced(20).removeFromBottom(36),
                       juce::Justification::centredLeft, true);
        }
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (std::abs(wheel.deltaY) < 0.001f)
            return;

        emitZoom(wheel.deltaY > 0.0f, event.position);
    }

    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override
    {
        if (std::abs(scaleFactor - 1.0f) < 0.01f)
            return;

        emitZoom(scaleFactor > 1.0f, event.position);
    }

private:
    void emitZoom(bool zoomIn, juce::Point<float> position)
    {
        const auto normalized = juce::Point<float>(
            juce::jlimit(0.0f, 1.0f, position.x / static_cast<float>(getWidth())),
            juce::jlimit(0.0f, 1.0f, position.y / static_cast<float>(getHeight())));

        client.sendZoom(zoomIn ? zoomInAddress : zoomOutAddress, normalized);

        lastGestureText = (zoomIn ? "ZOOM IN " : "ZOOM OUT ")
            + juce::String(normalized.x, 2) + ", " + juce::String(normalized.y, 2);
        repaint();
    }

    OscControlClient& client;
    juce::String lastGestureText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TouchSurfaceComponent)
};

class DialComponent final : public juce::Component
{
public:
    explicit DialComponent(OscControlClient& clientToUse)
        : client(clientToUse)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(6.0f);
        const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();

        juce::ColourGradient body(juce::Colour(0xffffe3bf), centre.x, bounds.getY(),
                                  juce::Colour(0xffd88632), centre.x, bounds.getBottom(), false);
        g.setGradientFill(body);
        g.fillEllipse(bounds);

        g.setColour(juce::Colour(0xff6b300d));
        g.drawEllipse(bounds, 3.0f);

        juce::Path marker;
        marker.addRoundedRectangle(-5.0f, -radius + 16.0f, 10.0f, 30.0f, 4.0f);
        g.setColour(juce::Colour(0xff151515));
        g.fillPath(marker, juce::AffineTransform::rotation(angleRadians).translated(centre.x, centre.y));

        g.setColour(juce::Colour(0xffb65312));
        g.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
        g.drawFittedText(statusText, getLocalBounds().reduced(20).withTrimmedTop(getHeight() / 2 + 52),
                         juce::Justification::centred, 2);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        lastDragPoint = event.position;
        client.sendClick();
        statusText = "CLICK";
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        const auto deltaY = event.position.y - lastDragPoint.y;
        dragAccumulator -= deltaY;
        lastDragPoint = event.position;
        flushAccumulator();
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        dragAccumulator += wheel.deltaY * 14.0f;
        flushAccumulator();
    }

private:
    void flushAccumulator()
    {
        constexpr float pixelsPerStep = 12.0f;

        while (dragAccumulator >= pixelsPerStep)
        {
            dragAccumulator -= pixelsPerStep;
            emitIncrement(1);
        }

        while (dragAccumulator <= -pixelsPerStep)
        {
            dragAccumulator += pixelsPerStep;
            emitDecrement(1);
        }
    }

    void emitIncrement(int steps)
    {
        angleRadians += 0.18f * static_cast<float>(steps);
        client.sendStepDelta(incrementAddress, steps);
        statusText = "INC " + juce::String(steps);
        repaint();
    }

    void emitDecrement(int steps)
    {
        angleRadians -= 0.18f * static_cast<float>(steps);
        client.sendStepDelta(decrementAddress, steps);
        statusText = "DEC " + juce::String(steps);
        repaint();
    }

    OscControlClient& client;
    juce::Point<float> lastDragPoint;
    float dragAccumulator = 0.0f;
    float angleRadians = 0.0f;
    juce::String statusText { "READY" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DialComponent)
};
} // namespace

HardwareEmulatorMainComponent::HardwareEmulatorMainComponent()
{
    static OscControlClient controlClient;

    touchSurface = std::make_unique<TouchSurfaceComponent>(controlClient);
    dial = std::make_unique<DialComponent>(controlClient);
    smallScreen = std::make_unique<SmallScreenComponent>();

    addAndMakeVisible(*touchSurface);
    addAndMakeVisible(*dial);
    addAndMakeVisible(*smallScreen);

    setSize(1180, 720);
}

HardwareEmulatorMainComponent::~HardwareEmulatorMainComponent() = default;

void HardwareEmulatorMainComponent::paint(juce::Graphics& g)
{
    juce::ColourGradient background(juce::Colour(0xffffe4bf), 0.0f, 0.0f,
                                    juce::Colour(0xffde7c1f), static_cast<float>(getWidth()), static_cast<float>(getHeight()), false);
    g.setGradientFill(background);
    g.fillAll();

    auto panelBounds = getLocalBounds().reduced(36).toFloat();
    g.setColour(juce::Colour(0x55fff4e3));
    g.fillRoundedRectangle(panelBounds, 22.0f);

    g.setColour(juce::Colour(0xff6a2e08));
    g.drawRoundedRectangle(panelBounds, 22.0f, 3.0f);

    g.setColour(juce::Colour(0xff5a2304));
    g.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    g.drawText("Tracker Hardware Control Surface Emulator", 54, 34, getWidth() - 108, 30, juce::Justification::centred);
}

void HardwareEmulatorMainComponent::resized()
{
    auto area = getLocalBounds().reduced(70, 90);
    auto right = area.removeFromRight(340);
    area.removeFromRight(28);

    touchSurface->setBounds(area);
    smallScreen->setBounds(right.removeFromTop(126).reduced(8));
    right.removeFromTop(28);
    dial->setBounds(right.withTrimmedTop(10).reduced(12));
}
