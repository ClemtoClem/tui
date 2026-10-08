/**
 * @file Tui.hpp
 * @brief En-tête ombrelle : inclut toute l'API publique du framework TUI.
 *
 * Confort à la cpp-tui pour les consommateurs (un seul #include), même
 * si en interne le framework reste découpé en de nombreux fichiers
 * pour des raisons de temps de compilation et de testabilité.
 */

#pragma once

// Core
#include "core/Cell.hpp"
#include "core/Color.hpp"
#include "core/Event.hpp"
#include "core/Geometry.hpp"
#include "core/SyntaxHighlight.hpp"
#include "core/Text.hpp"
#include "core/TextStyle.hpp"
#include "core/Theme.hpp"
#include "core/Timer.hpp"

// Terminal
#include "terminal/AnsiScreen.hpp"
#include "terminal/HeadlessTerminalBackend.hpp"
#include "terminal/ITerminalBackend.hpp"
#include "terminal/PosixTerminalBackend.hpp"
#include "terminal/VTParser.hpp"

// Runtime
#include "App.hpp"
#include "Buffer.hpp"

// Widget base
#include "widget/BoxFrame.hpp"
#include "widget/Container.hpp"
#include "widget/FocusManager.hpp"
#include "widget/Layout.hpp"
#include "widget/Scrollbar.hpp"
#include "widget/Widget.hpp"

// Layout containers
#include "widget/layout/Align.hpp"
#include "widget/layout/Border.hpp"
#include "widget/layout/Grid.hpp"
#include "widget/layout/Horizontal.hpp"
#include "widget/layout/ScrollableContainer.hpp"
#include "widget/layout/SplitPane.hpp"
#include "widget/layout/Stack.hpp"
#include "widget/layout/Tabs.hpp"
#include "widget/layout/Vertical.hpp"

// Widgets
#include "widgets/Badge.hpp"
#include "widgets/Breadcrumb.hpp"
#include "widgets/Button.hpp"
#include "widgets/Checkbox.hpp"
#include "widgets/CheckboxList.hpp"
#include "widgets/CodeEditor.hpp"
#include "widgets/Dialog.hpp"
#include "widgets/Dropdown.hpp"
#include "widgets/Input.hpp"
#include "widgets/Label.hpp"
#include "widgets/ListView.hpp"
#include "widgets/MenuBar.hpp"
#include "widgets/MenuDialog.hpp"
#include "widgets/MenuItem.hpp"
#include "widgets/Notification.hpp"
#include "widgets/NumberInput.hpp"
#include "widgets/ProgressBar.hpp"
#include "widgets/RadioSet.hpp"
#include "widgets/SelectionState.hpp"
#include "widgets/ShortcutBar.hpp"
#include "widgets/Spinner.hpp"
#include "widgets/StatusBar.hpp"
#include "widgets/TableView.hpp"
#include "widgets/Terminal.hpp"
#include "widgets/TextArea.hpp"
#include "widgets/ToggleSwitch.hpp"
#include "widgets/Tooltip.hpp"
#include "widgets/TreeView.hpp"
#include "widgets/UndoRedoHistory.hpp"
