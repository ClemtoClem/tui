#include <gtest/gtest.h>
#include <tui/widget/FocusManager.hpp>
#include <tui/widget/layout/Vertical.hpp>
#include <tui/widgets/Button.hpp>
#include <tui/widgets/Dialog.hpp>

using namespace tui;

TEST(Dialog, HiddenByDefault) {
    Dialog dialog;
    EXPECT_FALSE(dialog.showing());
    EXPECT_FALSE(dialog.visible());
}

TEST(Dialog, ShowHideTogglesVisibility) {
    Dialog dialog;
    dialog.show();
    EXPECT_TRUE(dialog.visible());
    dialog.hide();
    EXPECT_FALSE(dialog.visible());
}

TEST(Dialog, EscapeKeyCloses) {
    auto dialog = std::make_shared<Dialog>();
    dialog->show();
    ASSERT_TRUE(dialog->showing());
    dialog->on_key(KeyEvent{Key::Escape});
    EXPECT_FALSE(dialog->showing());
}

TEST(Dialog, FocusCannotEscapeOpenModalScope) {
    auto root = std::make_shared<Vertical>();
    auto background_btn = std::make_shared<Button>("Background");
    root->add_child(background_btn);

    auto dialog = std::make_shared<Dialog>();
    auto dialog_content = std::make_shared<Vertical>();
    auto ok_btn = std::make_shared<Button>("OK");
    auto cancel_btn = std::make_shared<Button>("Cancel");
    dialog_content->add_child(ok_btn);
    dialog_content->add_child(cancel_btn);
    dialog->set_content(dialog_content);
    root->add_child(dialog);

    FocusManager fm;
    dialog->set_focus_manager(&fm);
    dialog->arrange(Rect{0, 0, 80, 24});
    dialog->show();

    fm.focus_next(root);
    EXPECT_EQ(fm.current(), ok_btn);
    fm.focus_next(root);
    EXPECT_EQ(fm.current(), cancel_btn);
    fm.focus_next(root); // doit boucler DANS la modale, jamais atteindre background_btn
    EXPECT_EQ(fm.current(), ok_btn);
}

TEST(Dialog, ClosingRestoresPriorFocusScope) {
    auto root = std::make_shared<Vertical>();
    auto background_btn = std::make_shared<Button>("Background");
    root->add_child(background_btn);

    auto dialog = std::make_shared<Dialog>();
    auto dialog_content = std::make_shared<Vertical>();
    auto ok_btn = std::make_shared<Button>("OK");
    dialog_content->add_child(ok_btn);
    dialog->set_content(dialog_content);
    root->add_child(dialog);

    FocusManager fm;
    dialog->set_focus_manager(&fm);
    dialog->arrange(Rect{0, 0, 80, 24});

    dialog->show();
    fm.focus_next(root);
    EXPECT_EQ(fm.current(), ok_btn);

    dialog->hide();
    fm.focus_next(root); // scope restauré : le fond redevient atteignable
    EXPECT_EQ(fm.current(), background_btn);
}

TEST(Dialog, HiddenDialogContentIsNotFocusable) {
    auto root = std::make_shared<Vertical>();
    auto background_btn = std::make_shared<Button>("Background");
    root->add_child(background_btn);

    auto dialog = std::make_shared<Dialog>();
    auto dialog_content = std::make_shared<Vertical>();
    auto ok_btn = std::make_shared<Button>("OK");
    dialog_content->add_child(ok_btn);
    dialog->set_content(dialog_content);
    root->add_child(dialog);

    // Dialog jamais montrée : sa présence dans l'arbre ne doit pas
    // rendre son contenu atteignable par la traversée normale.
    FocusManager fm;
    fm.focus_next(root);
    EXPECT_EQ(fm.current(), background_btn);
}

TEST(Dialog, MouseEventsAreAbsorbedWhileOpen) {
    Dialog dialog;
    dialog.show();
    EXPECT_TRUE(dialog.on_mouse(MouseEvent{0, 0, MouseEvent::Button::Left, MouseEvent::Action::Press, false, false, false}));
}

TEST(Dialog, MouseEventsPassThroughWhenClosed) {
    Dialog dialog;
    EXPECT_FALSE(dialog.on_mouse(MouseEvent{0, 0, MouseEvent::Button::Left, MouseEvent::Action::Press, false, false, false}));
}
