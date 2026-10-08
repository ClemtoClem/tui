#include <gtest/gtest.h>
#include <tui/App.hpp>
#include <tui/terminal/HeadlessTerminalBackend.hpp>
#include <tui/widget/layout/Horizontal.hpp>

#include <atomic>
#include <thread>

using namespace tui;

namespace {

// Note : plusieurs événements poussés via HeadlessTerminalBackend ne sont
// traités qu'un par un, un par itération de App::run(). Un minuteur à 0ms
// programmé AVANT run() devient "dû" dès la toute première itération (le
// temps réel écoulé pendant backend_->init() etc. suffit à dépasser
// l'échéance), donc il ne faut JAMAIS l'utiliser pour "attendre que tous
// les événements poussés soient traités" - un seul événement est garanti
// traité avant qu'un minuteur à 0ms ne se déclenche dans la même itération
// (handle_event() précède toujours run_due() dans la boucle), mais pas
// plusieurs. Ces tests déclenchent donc quit() depuis le callback
// after_key/after_mouse du DERNIER événement attendu, jamais depuis un
// minuteur, dès qu'ils poussent plus d'un événement.
class RecordingWidget : public Widget {
public:
    std::atomic<int> key_count{0};
    std::atomic<int> mouse_count{0};
    std::atomic<int> paint_count{0};
    bool consume_keys = true;
    std::function<void()> after_key;
    std::function<void()> after_mouse;

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return {c.max_width, c.max_height};
    }
    void paint(Buffer&) const override {
        ++const_cast<RecordingWidget*>(this)->paint_count;
    }
    bool on_key(const KeyEvent&) override {
        ++key_count;
        if (after_key) after_key();
        return consume_keys;
    }
    bool on_mouse(const MouseEvent&) override {
        ++mouse_count;
        if (after_mouse) after_mouse();
        return true;
    }
};

class FocusableWidget : public RecordingWidget {
public:
    [[nodiscard]] bool focusable() const override { return true; }
};

} // namespace

TEST(App, RunsAndQuitsViaTimer) {
    auto backend = std::make_unique<HeadlessTerminalBackend>(20, 10);
    App app(std::move(backend));
    auto widget = std::make_shared<RecordingWidget>();
    app.set_root(widget);

    app.add_timer(std::chrono::milliseconds(0), false, [&app] { app.quit(42); });

    int code = app.run();
    EXPECT_EQ(code, 42);
    EXPECT_GE(widget->paint_count.load(), 1);
}

TEST(App, KeyEventsRouteToRootWidget) {
    auto backend = std::make_unique<HeadlessTerminalBackend>(20, 10);
    auto* backend_ptr = backend.get();
    App app(std::move(backend));
    auto widget = std::make_shared<RecordingWidget>();
    app.set_root(widget);
    widget->after_key = [&] { if (widget->key_count.load() == 2) app.quit(0); };

    backend_ptr->push_event(KeyEvent{Key::Char, U'a'});
    backend_ptr->push_event(KeyEvent{Key::Char, U'b'});

    app.run();
    EXPECT_EQ(widget->key_count.load(), 2);
}

TEST(App, MouseEventsRouteToRootWidget) {
    auto backend = std::make_unique<HeadlessTerminalBackend>(20, 10);
    auto* backend_ptr = backend.get();
    App app(std::move(backend));
    auto widget = std::make_shared<RecordingWidget>();
    app.set_root(widget);
    widget->after_mouse = [&] { app.quit(0); };

    backend_ptr->push_event(MouseEvent{5, 5, MouseEvent::Button::Left, MouseEvent::Action::Press, false, false, false});

    app.run();
    EXPECT_EQ(widget->mouse_count.load(), 1);
}

TEST(App, PostFromAnotherThreadIsExecutedOnRunThread) {
    auto backend = std::make_unique<HeadlessTerminalBackend>(20, 10);
    App app(std::move(backend));
    auto widget = std::make_shared<RecordingWidget>();
    app.set_root(widget);

    std::atomic<bool> posted_ran{false};
    std::atomic<std::thread::id> posted_thread_id{};
    std::thread t([&app, &posted_ran, &posted_thread_id] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        app.post([&] {
            posted_ran = true;
            posted_thread_id = std::this_thread::get_id();
            app.quit(7);
        });
    });

    std::thread::id run_thread_id = std::this_thread::get_id();
    int code = app.run();
    t.join();

    EXPECT_TRUE(posted_ran.load());
    EXPECT_EQ(code, 7);
    EXPECT_EQ(posted_thread_id.load(), run_thread_id); // exécuté sur le thread de run(), pas celui du post()
}

TEST(App, InvalidateFromWidgetTriggersRepaint) {
    auto backend = std::make_unique<HeadlessTerminalBackend>(20, 10);
    App app(std::move(backend));
    auto widget = std::make_shared<RecordingWidget>();
    app.set_root(widget);

    app.add_timer(std::chrono::milliseconds(0), false, [&] {
        widget->need_repaint();
        app.quit(0);
    });

    app.run();
    EXPECT_GE(widget->paint_count.load(), 1);
}

TEST(App, ResizeEventResizesBuffer) {
    auto backend = std::make_unique<HeadlessTerminalBackend>(20, 10);
    auto* backend_ptr = backend.get();
    App app(std::move(backend));
    auto widget = std::make_shared<RecordingWidget>();
    app.set_root(widget);

    // En conditions réelles (PosixTerminalBackend), get_width()/get_height()
    // reflètent déjà la nouvelle taille au moment où poll_event() retourne
    // le ResizeEvent ; on reproduit ça ici en alignant les deux.
    backend_ptr->set_size(40, 15);
    backend_ptr->push_event(ResizeEvent{40, 15});
    app.add_timer(std::chrono::milliseconds(0), false, [&app] { app.quit(0); });

    app.run();
    EXPECT_GE(widget->paint_count.load(), 1);
}

TEST(App, TabMovesFocusInsteadOfReachingTheWidget) {
    auto backend = std::make_unique<HeadlessTerminalBackend>(20, 10);
    auto* backend_ptr = backend.get();
    App app(std::move(backend));

    auto root = std::make_shared<Horizontal>();
    auto a = std::make_shared<FocusableWidget>();
    auto b = std::make_shared<FocusableWidget>();
    root->add_child(a, LayoutParams::stretch(1));
    root->add_child(b, LayoutParams::stretch(1));
    app.set_root(root);
    a->after_key = [&] { app.quit(0); }; // Tab lui-même n'atteint jamais un widget ; seul 'x' le fera

    backend_ptr->push_event(KeyEvent{Key::Tab});
    backend_ptr->push_event(KeyEvent{Key::Char, U'x'});

    app.run();
    EXPECT_EQ(app.focus_manager().current(), a);
    EXPECT_EQ(a->key_count.load(), 1);
    EXPECT_EQ(b->key_count.load(), 0);
}

TEST(App, MouseHitTestPicksTheNestedChildUnderThePoint) {
    auto backend = std::make_unique<HeadlessTerminalBackend>(20, 10);
    auto* backend_ptr = backend.get();
    App app(std::move(backend));

    auto root = std::make_shared<Horizontal>();
    auto left = std::make_shared<RecordingWidget>();
    auto right = std::make_shared<RecordingWidget>();
    root->add_child(left, LayoutParams::stretch(1));
    root->add_child(right, LayoutParams::stretch(1));
    app.set_root(root);
    right->after_mouse = [&] { app.quit(0); };

    // Colonne de droite (x=10..19 sur un écran de largeur 20).
    backend_ptr->push_event(MouseEvent{15, 0, MouseEvent::Button::Left, MouseEvent::Action::Press, false, false, false});

    app.run();
    EXPECT_EQ(right->mouse_count.load(), 1);
    EXPECT_EQ(left->mouse_count.load(), 0);
}
