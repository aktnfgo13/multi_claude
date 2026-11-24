#include "ui/ChainView.h"

#include "engine/EffectFactory.h"
#include "ui/LookAndFeel.h"

namespace mfx::ui {

ChainView::ChainView(plugin::Processor& p) : proc_(p) {}

void ChainView::rebuild() {
    layoutBlocks();
    if (selected_ >= 0 && !proc_.engine().effect(selected_)) setSelected(-1);
    repaint();
}

void ChainView::setSelected(int slot) {
    selected_ = slot;
    if (onSelectionChanged) onSelectionChanged(slot);
    repaint();
}

void ChainView::layoutBlocks() {
    blocks_.clear();
    lanes_.clear();
    auto& e = proc_.engine();
    std::vector<Lane> rows = {Lane::Pre};
    if (e.isParallel()) { rows.push_back(Lane::A); rows.push_back(Lane::B); }
    else rows.push_back(Lane::A); // serial: lane A is just a continuation (lane B content shown with A)
    rows.push_back(Lane::Post);
    const float rowH = juce::jmin(86.0f, (getHeight() - 12.0f) / (float)rows.size());
    float y = 6.0f;
    for (Lane lane : rows) {
        auto row = juce::Rectangle<float>(8.0f, y, (float)getWidth() - 16.0f, rowH - 8.0f);
        auto slots = e.laneSlots(lane);
        if (!e.isParallel() && lane == Lane::A) { auto b = e.laneSlots(Lane::B); slots.insert(slots.end(), b.begin(), b.end()); }
        const float bw = juce::jmin(132.0f, (row.getWidth() - 110.0f) / (float)juce::jmax(1, (int)slots.size()));
        float x = row.getX() + 70.0f;
        for (size_t i = 0; i < slots.size(); ++i) {
            blocks_.push_back({slots[i], lane, (int)i, juce::Rectangle<float>(x, row.getY() + 6, bw - 8, row.getHeight() - 12)});
            x += bw;
        }
        lanes_.push_back({lane, row, juce::Rectangle<float>(x, row.getCentreY() - 14, 28, 28)});
        y += rowH;
    }
}

void ChainView::paint(juce::Graphics& g) {
    layoutBlocks();
    auto& e = proc_.engine();
    g.fillAll(colours::panel);
    for (const auto& l : lanes_) {
        g.setColour(colours::panelLight.withAlpha(0.5f));
        g.fillRoundedRectangle(l.r, 6.0f);
        g.setColour(colours::textDim);
        g.setFont(juce::FontOptions(12.0f));
        juce::String name = l.lane == Lane::Pre ? "PRE" : l.lane == Lane::Post ? "POST" : (e.isParallel() ? (l.lane == Lane::A ? "LANE A" : "LANE B") : "CHAIN");
        g.drawText(name, l.r.withWidth(64).reduced(6, 0), juce::Justification::centredLeft);
        // signal line
        g.setColour(colours::outline);
        g.drawLine(l.r.getX() + 64, l.r.getCentreY(), l.plus.getX(), l.r.getCentreY(), 2.0f);
        g.setColour(colours::accent);
        g.drawEllipse(l.plus, 1.5f);
        g.drawText("+", l.plus, juce::Justification::centred);
    }
    for (const auto& b : blocks_) {
        Effect* fx = e.effect(b.slot);
        if (!fx) continue;
        const bool byp = e.isBypassed(b.slot);
        auto r = b.r;
        if (dragging_ && b.slot == dragSlot_) r = r.withCentre(dragPos_);
        g.setColour(byp ? colours::panelLight : colours::panelLight.brighter(0.08f));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(colours::forCategory(fx->category()).withAlpha(byp ? 0.35f : 1.0f));
        g.fillRoundedRectangle(r.withHeight(5.0f), 2.0f);
        g.setColour(b.slot == selected_ ? colours::accent : colours::outline);
        g.drawRoundedRectangle(r, 6.0f, b.slot == selected_ ? 2.0f : 1.0f);
        g.setColour(byp ? colours::textDim : colours::text);
        g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText(fx->displayName(), r.reduced(6, 10).toNearestInt(), juce::Justification::centred, 2);
        // bypass LED
        g.setColour(byp ? colours::outline : colours::accent);
        g.fillEllipse(r.getRight() - 14, r.getBottom() - 14, 8, 8);
        if (fx->latencySamples() > 0) {
            g.setColour(colours::textDim);
            g.setFont(juce::FontOptions(10.0f));
            g.drawText(juce::String(fx->latencySamples()) + " smp", r.withTrimmedTop(r.getHeight() - 16).withTrimmedLeft(6).toNearestInt(), juce::Justification::centredLeft);
        }
    }
}

const ChainView::Block* ChainView::hit(juce::Point<float> p) const {
    for (const auto& b : blocks_) if (b.r.contains(p)) return &b;
    return nullptr;
}

bool ChainView::dropTarget(juce::Point<float> p, Lane& lane, int& index) const {
    for (const auto& l : lanes_) {
        if (!l.r.contains(p)) continue;
        lane = l.lane;
        index = 0;
        for (const auto& b : blocks_)
            if (b.lane == l.lane && b.slot != dragSlot_ && b.r.getCentreX() < p.x) ++index;
        return true;
    }
    return false;
}

void ChainView::mouseDown(const juce::MouseEvent& ev) {
    const auto p = ev.position;
    for (const auto& l : lanes_)
        if (l.plus.contains(p)) { showAddMenu(l.lane); return; }
    if (const Block* b = hit(p)) {
        setSelected(b->slot);
        if (ev.mods.isPopupMenu()) { showBlockMenu(*b); return; }
        dragSlot_ = b->slot;
        dragPos_ = p;
    }
}

void ChainView::mouseDrag(const juce::MouseEvent& ev) {
    if (dragSlot_ < 0) return;
    if (ev.getDistanceFromDragStart() > 6) dragging_ = true;
    dragPos_ = ev.position;
    repaint();
}

void ChainView::mouseUp(const juce::MouseEvent& ev) {
    if (dragging_ && dragSlot_ >= 0) {
        Lane lane; int index;
        if (dropTarget(ev.position, lane, index)) {
            // in serial mode the "CHAIN" row shows lanes A+B; drop into A
            proc_.moveEffect(dragSlot_, lane, index);
            if (onStructureChanged) onStructureChanged();
        }
    }
    dragging_ = false;
    dragSlot_ = -1;
    repaint();
}

void ChainView::mouseDoubleClick(const juce::MouseEvent& ev) {
    if (const Block* b = hit(ev.position)) {
        proc_.engine().setBypass(b->slot, !proc_.engine().isBypassed(b->slot));
        repaint();
    }
}

void ChainView::showAddMenu(Lane lane) {
    juce::PopupMenu menu;
    std::map<std::string, juce::PopupMenu> cats;
    const auto& list = availableEffects();
    for (size_t i = 0; i < list.size(); ++i) cats[list[i].category].addItem((int)i + 1, list[i].displayName);
    for (auto& [c, m] : cats) menu.addSubMenu(c, m);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this, lane](int r) {
        if (r <= 0) return;
        const int slot = proc_.addEffect(availableEffects()[(size_t)r - 1].typeId, lane);
        if (slot < 0) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Chain full", "All 16 slots are in use.");
        else setSelected(slot);
        if (onStructureChanged) onStructureChanged();
    });
}

void ChainView::showBlockMenu(const Block& b) {
    juce::PopupMenu m;
    const bool byp = proc_.engine().isBypassed(b.slot);
    m.addItem(1, byp ? "Enable" : "Bypass");
    m.addItem(2, "Remove");
    juce::PopupMenu move;
    move.addItem(10, "Pre");
    move.addItem(11, "Lane A");
    move.addItem(12, "Lane B");
    move.addItem(13, "Post");
    m.addSubMenu("Move to", move);
    const int slot = b.slot;
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this, slot, byp](int r) {
        if (r == 1) proc_.engine().setBypass(slot, !byp);
        else if (r == 2) { proc_.removeEffect(slot); setSelected(-1); }
        else if (r >= 10 && r <= 13) proc_.moveEffect(slot, (Lane)(r - 10), -1);
        if (onStructureChanged) onStructureChanged();
        repaint();
    });
}

} // namespace mfx::ui
