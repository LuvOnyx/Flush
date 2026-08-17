/*
  ==============================================================================
    VisageJuceHost.h
    Bridge for JUCE 8 + Visage (Fixed Rendering Pipeline)
  ==============================================================================
*/
#pragma once
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_events/juce_events.h>
#include "visage/app.h"
#include "visage/ui.h"
#include "visage/graphics.h"

// Crash Handler
static void npsCrashHandler(void*) {
    auto logFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                   .getChildFile("APC_CRASH_REPORT.txt");
    juce::String report = "TIME: " + juce::Time::getCurrentTime().toString(true, true) + "\n";
    report += juce::SystemStats::getStackBacktrace();
    logFile.replaceWithText(report);
}

/**
 * VisagePluginEditor - A JUCE AudioProcessorEditor that hosts Visage UI
 * 
 * This class properly integrates Visage's rendering pipeline with JUCE's OpenGL context.
 * 
 * Key concepts:
 * 1. Visage uses a Frame hierarchy where each Frame has a Region
 * 2. The Canvas manages rendering and needs regions added to it
 * 3. Frames must be initialized and have their event handlers set up
 * 4. The redraw() mechanism triggers actual drawing via drawToRegion()
 */
class VisagePluginEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    VisagePluginEditor(juce::AudioProcessor& p) : AudioProcessorEditor(&p) {
        static bool crashHandlerSet = false;
        if (!crashHandlerSet) {
            juce::SystemStats::setApplicationCrashHandler(npsCrashHandler);
            crashHandlerSet = true;
        }
        
        setOpaque(true);
        startTimerHz(60);
    }

    ~VisagePluginEditor() override {
        stopTimer();
        teardownVisage();
    }

    void paint(juce::Graphics& g) override { 
        g.fillAll(juce::Colours::black);

        if (windowless_ && backbuffer_.isValid()) {
            g.drawImageAt(backbuffer_, 0, 0);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        dispatchMouse(e, MouseDispatch::Down);
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        dispatchMouse(e, MouseDispatch::Drag);
    }

    void mouseUp(const juce::MouseEvent& e) override {
        dispatchMouse(e, MouseDispatch::Up);
    }

    void mouseMove(const juce::MouseEvent& e) override {
        dispatchMouse(e, MouseDispatch::Move);
    }

    // Forward a small, safe set of editing keys to the Visage frame tree. The
    // DAW/host may or may not grant the editor keyboard focus; when it does,
    // delete/backspace/arrows reach the active UI. Returns false for anything
    // we don't handle (so the host can keep it).
    bool keyPressed(const juce::KeyPress& key) override {
        if (!event_root_)
            return false;

        visage::KeyCode code = visage::KeyCode::Unknown;
        if (key == juce::KeyPress::backspaceKey)      code = visage::KeyCode::Backspace;
        else if (key == juce::KeyPress::deleteKey)    code = visage::KeyCode::Delete;
        else if (key == juce::KeyPress::upKey)        code = visage::KeyCode::Up;
        else if (key == juce::KeyPress::downKey)      code = visage::KeyCode::Down;
        else if (key == juce::KeyPress::leftKey)      code = visage::KeyCode::Left;
        else if (key == juce::KeyPress::rightKey)     code = visage::KeyCode::Right;
        else if (key == juce::KeyPress::escapeKey)    code = visage::KeyCode::Escape;
        else if (key == juce::KeyPress::returnKey)    code = visage::KeyCode::Return;
        else
            return false;

        visage::KeyEvent evt(code, 0, true, false);
        return event_root_->processKeyPress(evt);
    }

    void resized() override { 
        onResize(getWidth(), getHeight()); 
        if (canvas_) {
            if (windowless_) {
                canvas_->setWindowless(getWidth(), getHeight());
            } else {
                canvas_->setDimensions(getWidth(), getHeight());
            }
        }
    }

    void timerCallback() override {
        if (!rendererInitialized_) {
            tryInitialize();
            return;
        }

        if (!canvas_)
            return;

        onRender();
        drawStaleFrames();
        canvas_->submit();

        if (windowless_) {
            updateBackbufferFromScreenshot(canvas_->takeScreenshot());
            repaint();
        }
    }

    // Override these in your subclass
    virtual void onInit() {}
    virtual void onRender() {}
    virtual void onDestroy() {}
    virtual void onResize(int w, int h) {}

    // Change the repaint rate at runtime (60 / 120 / uncapped). The timer is the
    // only thing that drives `onRender`, so this is the "UI refresh" knob.
    void setRefreshHz(int hz) {
        if (hz <= 0) hz = 240;                       // "uncapped" -> host-limited
        stopTimer();
        startTimerHz(hz);
    }

protected:
    visage::Canvas& getCanvas() { return *canvas_; }
    visage::FrameEventHandler& getEventHandler() { return eventHandler_; }
    void setEventRoot(visage::Frame* root) { event_root_ = root; }
    
    /**
     * Add a frame to the canvas for rendering.
     * This sets up the frame's region and event handler.
     */
    void addFrameToCanvas(visage::Frame* frame) {
        if (!canvas_ || !frame)
            return;
            
        // Add the frame's region to the canvas
        canvas_->addRegion(frame->region());
        
        // Set up the event handler so redraw() works
        frame->setEventHandler(&eventHandler_);
        
        // Set DPI scale
        frame->setDpiScale((float)getDesktopScaleFactor());
        
        // Initialize the frame
        frame->init();
        
        // Trigger initial redraw
        frame->redrawAll();
    }
    
    /**
     * Remove a frame from the canvas.
     */
    void removeFrameFromCanvas(visage::Frame* frame) {
        if (!frame)
            return;
            
        // Clear event handler
        frame->setEventHandler(nullptr);
        
        // Remove from stale list
        auto pos = std::find(staleFrames_.begin(), staleFrames_.end(), frame);
        if (pos != staleFrames_.end())
            staleFrames_.erase(pos);
    }
    
    /**
     * Draw all frames that need redrawing.
     * This is called automatically in renderOpenGL().
     */
    void drawStaleFrames() {
        if (!canvas_)
            return;
            
        // Swap stale list to avoid issues if redraw() is called during draw
        std::vector<visage::Frame*> drawing;
        std::swap(staleFrames_, drawing);
        
        for (visage::Frame* frame : drawing) {
            if (frame && frame->isDrawing())
                frame->drawToRegion(*canvas_);
        }
        
        // Handle any frames that were added during drawing
        for (auto it = staleFrames_.begin(); it != staleFrames_.end();) {
            visage::Frame* frame = *it;
            if (std::find(drawing.begin(), drawing.end(), frame) == drawing.end()) {
                if (frame && frame->isDrawing())
                    frame->drawToRegion(*canvas_);
                it = staleFrames_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    enum class MouseDispatch {
        Down,
        Drag,
        Up,
        Move
    };

    void dispatchMouse(const juce::MouseEvent& e, MouseDispatch type) {
        if (!event_root_)
            return;

        // --- hit-test + coordinate conversion (matches Visage's own
        //     window_event_handler.cpp) -------------------------------------
        // Visage hit-testing and layout use LOGICAL (DPI-scaled) coordinates,
        // while JUCE delivers NATIVE pixels. The root frame sits at the editor
        // origin, so the editor position == the Visage window position, divided
        // by the desktop scale factor.
        const float scale = std::max (1.0f, (float)getDesktopScaleFactor());
        const visage::Point windowPos {
            static_cast<float>(e.position.x) / scale,
            static_cast<float>(e.position.y) / scale
        };

        // Down / Move re-hit-test the deepest frame under the cursor; Drag / Up
        // continue on the frame that received the Down (so a knob keeps the drag
        // even if the cursor leaves its bounds).
        visage::Frame* target = nullptr;
        switch (type) {
            case MouseDispatch::Down:
                target = event_root_->frameAtPoint (windowPos);
                mouse_down_frame_ = (target != nullptr) ? target : event_root_;
                target = mouse_down_frame_;
                break;
            case MouseDispatch::Drag:
            case MouseDispatch::Up:
                target = mouse_down_frame_ ? mouse_down_frame_ : event_root_;
                if (type == MouseDispatch::Up)
                    mouse_down_frame_ = nullptr;
                break;
            case MouseDispatch::Move:
                target = event_root_->frameAtPoint (windowPos);
                if (target == nullptr)
                    target = event_root_;
                break;
        }
        if (target == nullptr)
            return;

        // Convert the window position into the target frame's local coordinates.
        const visage::Point localPos = windowPos - target->positionInWindow();

        visage::MouseEvent me;
        me.event_frame = target;
        me.position = localPos;
        me.relative_position = localPos;
        me.window_position = windowPos;

        int mods = visage::kModifierNone;
        if (e.mods.isShiftDown()) mods |= visage::kModifierShift;
        if (e.mods.isCtrlDown())  mods |= visage::kModifierRegCtrl;
        if (e.mods.isAltDown())   mods |= visage::kModifierAlt;
        if (e.mods.isCommandDown()) mods |= visage::kModifierCmd;
        me.modifiers = mods;

        int buttons = visage::kMouseButtonNone;
        if (e.mods.isLeftButtonDown())   buttons |= visage::kMouseButtonLeft;
        if (e.mods.isMiddleButtonDown()) buttons |= visage::kMouseButtonMiddle;
        if (e.mods.isRightButtonDown())  buttons |= visage::kMouseButtonRight;
        me.button_state = buttons;

        if (type == MouseDispatch::Down) {
            last_button_id_ = e.mods.isLeftButtonDown() ? visage::kMouseButtonLeft :
                              e.mods.isRightButtonDown() ? visage::kMouseButtonRight :
                              e.mods.isMiddleButtonDown() ? visage::kMouseButtonMiddle :
                              visage::kMouseButtonLeft;
        }
        me.button_id = last_button_id_;
        me.is_down = (type != MouseDispatch::Up);

        switch (type) {
            case MouseDispatch::Down: target->processMouseDown (me); break;
            case MouseDispatch::Drag: target->processMouseDrag (me); break;
            case MouseDispatch::Up:   target->processMouseUp   (me); break;
            case MouseDispatch::Move: target->processMouseMove (me); break;
        }
    }

    void tryInitialize() {
        if (rendererInitialized_)
            return;

        auto* peer = getPeer();
        if (!peer)
            return;

        void* nativeWindow = peer->getNativeHandle();
        if (!nativeWindow)
            return;

        visage::Renderer::instance().initialize(nativeWindow, nullptr);

        canvas_ = std::make_unique<visage::Canvas>();

        // TEMP: Swap-chain path is unstable in plugin hosting. Use windowless render for preview.
        constexpr bool kForceWindowless = true;
        if (kForceWindowless || !visage::Canvas::swapChainSupported()) {
            windowless_ = true;
            canvas_->setWindowless(getWidth(), getHeight());
        } else {
            windowless_ = false;
            canvas_->pairToWindow(nativeWindow, getWidth(), getHeight());
        }

        canvas_->setDpiScale((float)getDesktopScaleFactor());

        eventHandler_.request_redraw = [this](visage::Frame* frame) {
            if (std::find(staleFrames_.begin(), staleFrames_.end(), frame) == staleFrames_.end())
                staleFrames_.push_back(frame);
        };

        eventHandler_.remove_from_hierarchy = [this](visage::Frame* frame) {
            auto pos = std::find(staleFrames_.begin(), staleFrames_.end(), frame);
            if (pos != staleFrames_.end())
                staleFrames_.erase(pos);
        };

        rendererInitialized_ = true;
        onInit();
    }

    void teardownVisage() {
        rendererInitialized_ = false;
        staleFrames_.clear();
        onDestroy();
        if (canvas_) {
            canvas_->removeFromWindow();
            canvas_.reset();
        }
        windowless_ = false;
        backbuffer_ = juce::Image();
    }

    void updateBackbufferFromScreenshot(const visage::Screenshot& shot) {
        if (shot.width() <= 0 || shot.height() <= 0)
            return;

        if (!backbuffer_.isValid() || backbuffer_.getWidth() != shot.width() || backbuffer_.getHeight() != shot.height()) {
            backbuffer_ = juce::Image(juce::Image::ARGB, shot.width(), shot.height(), true);
        }

        juce::Image::BitmapData data(backbuffer_, juce::Image::BitmapData::writeOnly);
        const uint8_t* src = shot.data();
        for (int y = 0; y < shot.height(); ++y) {
            auto* dst = reinterpret_cast<juce::PixelARGB*>(data.getLinePointer(y));
            const uint8_t* row = src + (y * shot.width() * 4);
            for (int x = 0; x < shot.width(); ++x) {
                const uint8_t r = row[x * 4 + 0];
                const uint8_t g = row[x * 4 + 1];
                const uint8_t b = row[x * 4 + 2];
                const uint8_t a = row[x * 4 + 3];
                dst[x].setARGB(a, r, g, b);
            }
        }
    }

    std::unique_ptr<visage::Canvas> canvas_;
    visage::FrameEventHandler eventHandler_;
    std::vector<visage::Frame*> staleFrames_;
    bool rendererInitialized_ = false;
    bool windowless_ = false;
    juce::Image backbuffer_;
    visage::Frame* event_root_ = nullptr;
    visage::Frame* mouse_down_frame_ = nullptr;   // receives Drag/Up after a Down
    visage::MouseButton last_button_id_ = visage::kMouseButtonLeft;
};
