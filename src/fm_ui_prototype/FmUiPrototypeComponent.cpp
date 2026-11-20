#include "FmUiPrototypeComponent.h"

#include <array>
#include <cmath>

namespace
{
struct OperatorStudy
{
    const char* name;
    const char* wave;
    float ratio;
    float output;
    std::array<int, 4> rates;
    std::array<float, 4> levels;
    float index;
};

constexpr std::array<OperatorStudy, 4> evolveOperators {{
    { "OP1", "SIN",  1.00f, 100.0f, { 85,  9,  6, 24 }, { 100.0f,  82.0f, 95.0f, 0.0f }, 0.0f },
    { "OP2", "TRI",  1.00f,   0.0f, { 12,  6,  9, 24 }, {  15.0f, 100.0f, 30.0f, 0.0f }, 3.4f },
    { "OP3", "ODD5", 1.41f,   0.0f, {  8, 13,  4, 22 }, {  75.0f,   8.0f, 95.0f, 0.0f }, 2.2f },
    { "OP4", "SAW",  0.37f,   0.0f, { 15,  7, 11, 20 }, {  20.0f, 100.0f, 12.0f, 0.0f }, 2.8f }
}};

const auto kBackground = juce::Colour(0xff02040a);
const auto kCell = juce::Colour(0xff151d24);
const auto kCellDark = juce::Colour(0xff0b1118);
const auto kCyan = juce::Colour(0xff00f6ff);
const auto kTeal = juce::Colour(0xff3de6c0);
const auto kYellow = juce::Colour(0xffffd21a);
const auto kViolet = juce::Colour(0xff5929e0);
const auto kRed = juce::Colour(0xffff3b2f);

float rateSeconds(int rate)
{
    const auto normalized = static_cast<float>(juce::jlimit(0, 99, rate)) / 99.0f;
    return 20.0f * std::pow(0.000025f, normalized);
}

float envelopeLevel(const OperatorStudy& op, float seconds)
{
    float level = op.levels[3] * 0.01f;
    float elapsed = 0.0f;
    for (int stage = 0; stage < 3; ++stage)
    {
        const float duration = rateSeconds(op.rates[static_cast<std::size_t>(stage)]);
        const float target = op.levels[static_cast<std::size_t>(stage)] * 0.01f;
        if (seconds <= elapsed + duration)
            return level + (target - level) * ((seconds - elapsed) / duration);
        elapsed += duration;
        level = target;
    }
    return level;
}

std::vector<float> envelopeTrace(const OperatorStudy& op, int points = 64)
{
    std::vector<float> values;
    values.reserve(static_cast<std::size_t>(points));
    constexpr float timelineSeconds = 28.0f;
    for (int point = 0; point < points; ++point)
    {
        const auto t = timelineSeconds * static_cast<float>(point) / static_cast<float>(points - 1);
        values.push_back(envelopeLevel(op, t) * 2.0f - 1.0f);
    }
    return values;
}

TrackerUIComponent::CellState makeCell(const juce::String& text,
                                       juce::Colour fill = kCell,
                                       juce::Colour textColour = kTeal)
{
    TrackerUIComponent::CellState cell;
    cell.text = text.toStdString();
    cell.fillColor = fill;
    cell.textColor = textColour;
    cell.glowColor = kCyan;
    return cell;
}

void highlight(TrackerUIComponent::CellState& cell, juce::Colour colour = kCyan)
{
    cell.drawOutline = true;
    cell.outlineColor = colour;
    cell.outlineLineWidth = 2.5f;
    cell.glowColor = colour;
    cell.glow = 0.78f;
    cell.depthScale = 1.10f;
}

juce::String asPercent(float value)
{
    return juce::String(juce::roundToInt(value));
}
} // namespace

FmUiPrototypeComponent::FmUiPrototypeComponent()
{
    TrackerUIComponent::Style style;
    style.background = kBackground;
    style.lightColor = juce::Colour(0xffddf6e8);
    style.defaultGlowColor = kRed;
    style.ambientStrength = 0.32f;
    style.lightDirection = { 0.2f, 0.45f, 1.0f };
    style.glowPulseEnabled = false;
    style.drawEmptyCells = false;
    renderer.setStyle(style);
    renderer.setCellSize(1.20f, 0.55f);
    renderer.setCellDepth(0.08f);

    openGLContext.setRenderer(this);
    openGLContext.setContinuousRepainting(false);
    openGLContext.setComponentPaintingEnabled(true);
    openGLContext.attachTo(*this);
    setWantsKeyboardFocus(true);
    setSize(1280, 820);
    const auto requestedConcept = juce::SystemStats::getEnvironmentVariable("MYK_FM_UI_PROTOTYPE_CONCEPT", {}).trim().toUpperCase();
    if (requestedConcept == "ROUTE") activeConcept = Concept::route;
    else if (requestedConcept == "DEPTH") activeConcept = Concept::depth;
    const auto screenshotPath = juce::SystemStats::getEnvironmentVariable("MYK_FM_UI_PROTOTYPE_SCREENSHOT", {}).trim();
    if (screenshotPath.isNotEmpty())
        screenshotOutput = juce::File(screenshotPath);
    rebuildView();
    startTimerHz(30);
}

FmUiPrototypeComponent::~FmUiPrototypeComponent()
{
    stopTimer();
    openGLContext.detach();
}

void FmUiPrototypeComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(kBackground);
}

void FmUiPrototypeComponent::resized()
{
    rebuildView();
}

void FmUiPrototypeComponent::newOpenGLContextCreated()
{
    renderer.initOpenGL(getWidth(), getHeight());
}

void FmUiPrototypeComponent::renderOpenGL()
{
    renderer.setViewportBounds(getLocalBounds(), getHeight(), static_cast<float>(openGLContext.getRenderingScale()));
    renderer.renderUI();
    writeRequestedScreenshot();
}

void FmUiPrototypeComponent::openGLContextClosing()
{
    renderer.shutdownOpenGL();
}

void FmUiPrototypeComponent::writeRequestedScreenshot()
{
    if (screenshotWritten || screenshotOutput == juce::File())
        return;

    int viewport[4] { 0, 0, 0, 0 };
    juce::gl::glGetIntegerv(juce::gl::GL_VIEWPORT, viewport);
    const int width = viewport[2];
    const int height = viewport[3];
    if (width <= 0 || height <= 0)
        return;

    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    juce::gl::glReadPixels(0, 0, width, height, juce::gl::GL_RGBA, juce::gl::GL_UNSIGNED_BYTE, pixels.data());
    if (juce::gl::glGetError() != juce::gl::GL_NO_ERROR)
        return;

    juce::Image image(juce::Image::ARGB, width, height, true);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto* pixel = pixels.data() + (static_cast<std::size_t>(y * width + x) * 4u);
            image.setPixelAt(x, height - 1 - y,
                             juce::Colour::fromRGBA(pixel[0], pixel[1], pixel[2], pixel[3]));
        }

    screenshotOutput.getParentDirectory().createDirectory();
    juce::FileOutputStream stream(screenshotOutput);
    juce::PNGImageFormat format;
    if (stream.openedOk() && format.writeImageToStream(image, stream))
    {
        screenshotWritten = true;
        juce::MessageManager::callAsync([]
        {
            if (auto* app = juce::JUCEApplicationBase::getInstance())
                app->systemRequestedQuit();
        });
    }
}

void FmUiPrototypeComponent::timerCallback()
{
    if (held)
        heldSeconds += 1.0 / 30.0;
    rebuildView();
}

void FmUiPrototypeComponent::advanceConcept(int amount)
{
    const auto raw = (static_cast<int>(activeConcept) + amount + 3) % 3;
    activeConcept = static_cast<Concept>(raw);
    selectedField = 0;
    rebuildView();
}

void FmUiPrototypeComponent::adjustZoom(float amount)
{
    const float oldZoom = zoom.zoomLevel;
    zoom.zoomLevel = juce::jlimit(0.55f, 2.4f, zoom.zoomLevel + amount);
    if (std::abs(oldZoom - zoom.zoomLevel) > 0.0001f)
        rebuildView();
}

void FmUiPrototypeComponent::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (std::abs(wheel.deltaY) > 0.0001f)
        adjustZoom(wheel.deltaY * 0.35f);
}

void FmUiPrototypeComponent::mouseDown(const juce::MouseEvent& event)
{
    lastDragPosition = event.getPosition();
}

void FmUiPrototypeComponent::mouseDrag(const juce::MouseEvent& event)
{
    const auto delta = event.getPosition() - lastDragPosition;
    lastDragPosition = event.getPosition();
    const float scale = 0.02f / zoom.zoomLevel;
    drag.panX += static_cast<float>(delta.x) * scale;
    drag.panY -= static_cast<float>(delta.y) * scale;
    rebuildView();
}

bool FmUiPrototypeComponent::keyPressed(const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    if (code == '1') activeConcept = Concept::lanes;
    else if (code == '2') activeConcept = Concept::route;
    else if (code == '3') activeConcept = Concept::depth;
    else if (code == juce::KeyPress::tabKey) advanceConcept(key.getModifiers().isShiftDown() ? -1 : 1);
    else if (code == juce::KeyPress::leftKey) selectedOperator = (selectedOperator + 3) % 4;
    else if (code == juce::KeyPress::rightKey) selectedOperator = (selectedOperator + 1) % 4;
    else if (code == juce::KeyPress::upKey) selectedField = (selectedField + 7) % 8;
    else if (code == juce::KeyPress::downKey) selectedField = (selectedField + 1) % 8;
    else if (code == juce::KeyPress::pageUpKey) selectedBank = (selectedBank + 4) % 5;
    else if (code == juce::KeyPress::pageDownKey) selectedBank = (selectedBank + 1) % 5;
    else if (code == juce::KeyPress::returnKey || code == juce::KeyPress::spaceKey)
    {
        held = !held;
        if (held)
            heldSeconds = 0.0;
    }
    else if (code == '+' || code == '=') indexOffset = juce::jlimit(-4.0f, 4.0f, indexOffset + 0.25f);
    else if (code == '-' || code == '_') indexOffset = juce::jlimit(-4.0f, 4.0f, indexOffset - 0.25f);
    else if (code == juce::KeyPress::escapeKey)
    {
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
    }
    else
        return false;

    rebuildView();
    return true;
}

void FmUiPrototypeComponent::rebuildView()
{
    traces.clear();
    columnWidths.clear();
    renderer.setCellDepth(activeConcept == Concept::depth ? 0.36f : 0.08f);
    switch (activeConcept)
    {
        case Concept::lanes: buildLanesView(); break;
        case Concept::route: buildRouteView(); break;
        case Concept::depth: buildDepthView(); break;
    }
    renderer.setTraces(traces);
    renderer.updateUIState(cells, overlay, zoom, drag, &columnWidths);
    openGLContext.triggerRepaint();
}

void FmUiPrototypeComponent::buildLanesView()
{
    constexpr int columns = 12;
    constexpr int rows = 11;
    cells.assign(columns, std::vector<TrackerUIComponent::CellState>(rows));
    columnWidths.assign(columns, 1.0f);
    for (auto& column : cells)
        for (auto& cell : column)
            cell = makeCell({}, kCellDark);

    static constexpr std::array<const char*, 5> bankNames { "CORE", "ENV", "SCALE", "ROUTE", "GLOBAL" };
    for (int opIndex = 0; opIndex < 4; ++opIndex)
    {
        const auto& op = evolveOperators[static_cast<std::size_t>(opIndex)];
        const int base = opIndex * 3;
        cells[base][0] = makeCell(op.name, kCell, kYellow);
        cells[base + 1][0] = makeCell("RAT", kCell);
        cells[base + 2][0] = makeCell(juce::String(op.ratio, 2), kCell, kCyan);
        if (selectedBank == 0)
        {
            cells[base][4] = makeCell("WAVE", kCellDark);
            cells[base + 1][4] = makeCell(op.wave, kCell, kTeal);
            cells[base + 2][4] = makeCell("OUT " + asPercent(op.output), kCell);
            cells[base][5] = makeCell("MODE", kCellDark);
            cells[base + 1][5] = makeCell("RAT", kCell);
            cells[base + 2][5] = makeCell("CRS " + juce::String(op.ratio, 2), kCell);
            cells[base][6] = makeCell("FINE", kCellDark);
            cells[base + 1][6] = makeCell("0", kCell);
            cells[base + 2][6] = makeCell("DET 0", kCell);
            cells[base][7] = makeCell("FIX", kCellDark);
            cells[base + 1][7] = makeCell("440", kCell);
        }
        else if (selectedBank == 1)
            for (int stage = 0; stage < 4; ++stage)
            {
                const int row = 4 + stage;
                cells[base][row] = makeCell("R" + juce::String(stage + 1), kCellDark);
                cells[base + 1][row] = makeCell(juce::String(op.rates[static_cast<std::size_t>(stage)]), kCell);
                cells[base + 2][row] = makeCell("L " + asPercent(op.levels[static_cast<std::size_t>(stage)]), kCell);
            }
        else if (selectedBank == 2)
        {
            static constexpr std::array<const char*, 5> scale { "VEL", "BP", "LSL", "LSR", "RSCL" };
            for (int row = 0; row < 5; ++row)
            {
                cells[base][4 + row] = makeCell(scale[static_cast<std::size_t>(row)], kCellDark);
                cells[base + 1][4 + row] = makeCell(row == 1 ? "C3" : juce::String(opIndex == 0 ? 0 : 45 + opIndex * 10), kCell);
            }
        }
        else if (selectedBank == 3)
        {
            for (int target = 0; target < 4; ++target)
            {
                cells[base][4 + target] = makeCell("TO" + juce::String(target + 1), kCellDark);
                const float route = target == opIndex ? (opIndex == 3 ? 2.80f : 0.0f)
                    : (opIndex > target ? op.index + indexOffset : 0.0f);
                cells[base + 1][4 + target] = makeCell(route == 0.0f ? "--" : juce::String(route, 2), kCell, kViolet.brighter());
            }
            cells[base][8] = makeCell("OUT", kCellDark);
            cells[base + 1][8] = makeCell(asPercent(op.output), kCell);
        }
        else
        {
            static constexpr std::array<const char*, 10> global { "PSET", "ALG", "VOI", "LEV", "OCT", "GLIDE", "INDEX", "FEED", "COLOR", "DRIVE" };
            for (int index = opIndex; index < 10; index += 4)
            {
                const int row = 4 + index / 4;
                cells[base][row] = makeCell(global[static_cast<std::size_t>(index)], kCellDark);
                cells[base + 1][row] = makeCell(index == 0 ? "EVOLVE" : index == 1 ? "FDBK" : index == 6 ? "85" : "0", kCell, kViolet.brighter());
            }
        }

        if (opIndex == selectedOperator)
        {
            highlight(cells[base][0], kYellow);
            highlight(cells[base + 1][5 + (selectedField % 4)]);
        }

        TrackerUIComponent::Trace trace;
        trace.colStart = static_cast<float>(base);
        trace.colEnd = static_cast<float>(base + 3);
        trace.rowTop = 1.0f;
        trace.rowBottom = 4.0f;
        trace.samples = envelopeTrace(op);
        trace.color = opIndex == selectedOperator ? kYellow : kTeal;
        trace.z = 1.25f;
        trace.lineWidth = opIndex == selectedOperator ? 3.0f : 1.7f;
        trace.cacheId = "lane-envelope-" + std::to_string(opIndex);
        trace.cacheRevision = 1;
        if (held)
        {
            TrackerUIComponent::Trace::Marker marker;
            marker.position = juce::jlimit(0.0f, 1.0f, static_cast<float>(heldSeconds / 27.0));
            marker.sample = envelopeLevel(op, static_cast<float>(heldSeconds)) * 2.0f - 1.0f;
            marker.color = kRed;
            marker.size = 0.17f;
            trace.markers.push_back(marker);
        }
        traces.push_back(std::move(trace));
    }

    auto lanesHud = juce::String("FM\nLANES ") + bankNames[static_cast<std::size_t>(selectedBank)] + "\nOP" + juce::String(selectedOperator + 1);
    lanesHud << "\n" << (held ? "HOLD" : "READY");
    overlay.text = lanesHud.toStdString();
    overlay.color = kCyan;
    overlay.glowColor = kCyan;
    overlay.glowStrength = 0.25f;
    overlay.rightStrip = true;
}

void FmUiPrototypeComponent::buildRouteView()
{
    constexpr int columns = 14;
    constexpr int rows = 10;
    cells.assign(columns, std::vector<TrackerUIComponent::CellState>(rows));
    columnWidths.assign(columns, 1.0f);
    columnWidths[0] = 1.25f;
    columnWidths[7] = 0.38f;
    for (auto& column : cells)
        for (auto& cell : column)
            cell = makeCell({}, kCellDark);

    cells[0][0] = makeCell("PSET", kCellDark);
    cells[1][0] = makeCell("EVOLVE", kCell, kYellow);
    cells[0][1] = makeCell("ALG", kCellDark);
    cells[1][1] = makeCell("FDBK", kCell, kCyan);
    cells[0][2] = makeCell("INDEX", kCellDark);
    cells[1][2] = makeCell("85", kCell, kViolet.brighter());
    cells[0][3] = makeCell("FEED", kCellDark);
    cells[1][3] = makeCell("110", kCell, kViolet.brighter());
    cells[0][5] = makeCell("MATRIX", kCell, kYellow);
    const int matrixLeft = 2;
    for (int destination = 0; destination < 4; ++destination)
        cells[matrixLeft + 1 + destination][4] = makeCell("T" + juce::String(destination + 1), kCellDark);
    for (int source = 0; source < 4; ++source)
    {
        cells[matrixLeft][5 + source] = makeCell("OP" + juce::String(source + 1), kCellDark, kYellow);
        for (int destination = 0; destination < 4; ++destination)
        {
            float value = 0.0f;
            if (source == 1 && destination == 0) value = 3.4f + indexOffset;
            if (source == 2 && destination == 1) value = 2.2f + indexOffset;
            if (source == 3 && destination == 2) value = 1.4f + indexOffset;
            if (source == 3 && destination == 3) value = 2.8f + indexOffset;
            auto& cell = cells[matrixLeft + 1 + destination][5 + source];
            cell = makeCell(value == 0.0f ? "--" : juce::String(value, 2), value == 0.0f ? kCellDark : kCell, value == 0.0f ? kTeal.darker(0.55f) : kViolet.brighter());
            if (source == selectedOperator || destination == selectedOperator)
                cell.fillColor = cell.fillColor.interpolatedWith(kCyan, 0.12f);
            const int focusSource = selectedOperator == 0 ? 1 : selectedOperator;
            const int focusDestination = juce::jmax(0, focusSource - 1);
            if (source == focusSource && destination == focusDestination)
                highlight(cell, kCyan);
        }
    }

    for (int opIndex = 0; opIndex < 4; ++opIndex)
    {
        const int col = 8 + opIndex;
        const auto& op = evolveOperators[static_cast<std::size_t>(opIndex)];
        cells[col][0] = makeCell(op.name, opIndex == selectedOperator ? kCell : kCellDark, opIndex == selectedOperator ? kYellow : kTeal);
        cells[col][1] = makeCell(op.wave, kCell);
        cells[col][2] = makeCell("R " + juce::String(op.ratio, 2), kCell);
        cells[col][3] = makeCell("O " + asPercent(op.output), kCell);
        cells[col][4] = makeCell("R1 " + juce::String(op.rates[0]), kCellDark);
        cells[col][5] = makeCell("L1 " + asPercent(op.levels[0]), kCell);
        cells[col][6] = makeCell("R2 " + juce::String(op.rates[1]), kCellDark);
        cells[col][7] = makeCell("L2 " + asPercent(op.levels[1]), kCell);
        if (opIndex == selectedOperator)
            highlight(cells[col][0], kYellow);
    }

    auto routeHud = juce::String("FM\nROUTE\nOP") + juce::String(selectedOperator + 1);
    routeHud << "\n" << (held ? "HOLD" : "READY");
    overlay.text = routeHud.toStdString();
    overlay.color = kCyan;
    overlay.glowColor = kCyan;
    overlay.rightStrip = true;
}

void FmUiPrototypeComponent::buildDepthView()
{
    constexpr int columns = 14;
    constexpr int rows = 10;
    cells.assign(columns, std::vector<TrackerUIComponent::CellState>(rows));
    columnWidths.assign(columns, 1.0f);
    for (auto& column : cells)
        for (auto& cell : column)
            cell = makeCell({}, kCellDark);

    cells[0][0] = makeCell("DEPTH", kCell, kYellow);
    cells[1][0] = makeCell("Z", kCellDark);
    cells[2][0] = makeCell("OP4", kCellDark, kTeal);
    cells[4][0] = makeCell("OP3", kCellDark, kTeal);
    cells[6][0] = makeCell("OP2", kCellDark, kTeal);
    cells[8][0] = makeCell("OP1", kCellDark, kYellow);
    cells[10][0] = makeCell("OUT", kCellDark, kTeal);

    const std::array<int, 4> bases {{ 0, 3, 6, 9 }};
    const std::array<int, 4> rowsForOp {{ 7, 5, 3, 1 }};
    const std::array<float, 4> zForOp {{ 1.30f, 0.50f, -0.25f, -1.05f }};
    for (int opIndex = 3; opIndex >= 0; --opIndex)
    {
        const auto& op = evolveOperators[static_cast<std::size_t>(opIndex)];
        const int base = bases[static_cast<std::size_t>(opIndex)];
        const int row = rowsForOp[static_cast<std::size_t>(opIndex)];
        const float z = zForOp[static_cast<std::size_t>(opIndex)] + (opIndex == selectedOperator ? 0.30f : 0.0f);
        cells[base][row] = makeCell(op.name, kCell, opIndex == 0 ? kYellow : kTeal);
        cells[base + 1][row] = makeCell(op.wave, kCellDark);
        cells[base + 2][row] = makeCell("R " + juce::String(op.ratio, 2), kCell);
        cells[base][row + 1] = makeCell("ENV", kCellDark);
        cells[base + 1][row + 1] = makeCell("I " + juce::String(op.index + indexOffset, 2), kCell, kViolet.brighter());
        cells[base + 2][row + 1] = makeCell(opIndex == 0 ? "AUD" : "MOD", kCell);
        for (int column = base; column < base + 3; ++column)
            for (int localRow = row; localRow <= row + 1; ++localRow)
            {
                auto& cell = cells[column][localRow];
                cell.zOffset = z;
                cell.depthScale = 1.0f + (z + 1.1f) * 0.10f;
            }
        if (opIndex == selectedOperator)
        {
            highlight(cells[base][row], kYellow);
            highlight(cells[base + 1][row + 1], kCyan);
        }

        TrackerUIComponent::Trace envelope;
        envelope.colStart = static_cast<float>(base);
        envelope.colEnd = static_cast<float>(base + 3);
        envelope.rowTop = static_cast<float>(row);
        envelope.rowBottom = static_cast<float>(row + 1);
        envelope.samples = envelopeTrace(op, 32);
        envelope.color = opIndex == selectedOperator ? kYellow : kTeal;
        envelope.z = z + 1.9f;
        envelope.lineWidth = opIndex == selectedOperator ? 2.7f : 1.4f;
        traces.push_back(std::move(envelope));
    }

    TrackerUIComponent::Trace cascade;
    cascade.colStart = 1.3f;
    cascade.colEnd = 11.3f;
    cascade.rowTop = 1.0f;
    cascade.rowBottom = 9.0f;
    cascade.samples = { 0.78f, 0.72f, 0.62f, 0.50f, 0.38f, 0.24f, 0.08f, -0.08f, -0.23f, -0.38f, -0.54f, -0.70f };
    cascade.color = kCyan;
    cascade.z = 2.9f;
    cascade.lineWidth = 2.5f;
    traces.push_back(std::move(cascade));

    cells[12][5] = makeCell("SPEAKER", kCell, kTeal);
    cells[12][6] = makeCell("OP1 OUT", kCellDark);
    cells[12][7] = makeCell("100", kCell, kYellow);
    for (int row = 5; row <= 7; ++row)
    {
        cells[12][row].zOffset = 1.85f;
        cells[12][row].depthScale = 1.22f;
    }

    auto depthHud = juce::String("FM\nDEPTH\nOP") + juce::String(selectedOperator + 1);
    depthHud << "\n" << (held ? "HOLD" : "READY");
    overlay.text = depthHud.toStdString();
    overlay.color = kCyan;
    overlay.glowColor = kCyan;
    overlay.glowStrength = 0.25f;
    overlay.rightStrip = true;
}
