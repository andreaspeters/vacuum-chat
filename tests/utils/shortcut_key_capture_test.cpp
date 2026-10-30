#include <utils/shortcuts.h>

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>

#include <iostream>

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    QLineEdit editor;

    QKeyEvent shortcutPress(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier);
    if (!Shortcuts::captureKeySequenceKeyPress(editor, shortcutPress)) {
        std::cerr << "shortcut key press was not consumed\n";
        return 1;
    }
    const QString expected = QKeySequence(Qt::CTRL | Qt::Key_K)
                                 .toString(QKeySequence::NativeText);
    if (editor.text() != expected) {
        std::cerr << "captured shortcut did not match the pressed key sequence\n";
        return 1;
    }

    editor.setText(QStringLiteral("unchanged"));
    QKeyEvent modifierPress(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
    if (!Shortcuts::captureKeySequenceKeyPress(editor, modifierPress)) {
        std::cerr << "standalone modifier key press was not consumed\n";
        return 1;
    }
    if (editor.text() != QStringLiteral("unchanged")) {
        std::cerr << "standalone modifier overwrote the captured shortcut\n";
        return 1;
    }

    return 0;
}
