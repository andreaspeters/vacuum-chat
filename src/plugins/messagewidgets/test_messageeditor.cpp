#include "messageeditor.h"

#include <QApplication>
#include <QImage>
#include <QMimeData>
#include <QTextDocument>
#include <iostream>

namespace {

class TestMessageEditor : public MessageEditor
{
public:
    TestMessageEditor() : MessageEditor(nullptr) {}

    bool canPaste(const QMimeData *mimeData) const
    {
        return canInsertFromMimeData(mimeData);
    }

    void insertMime(const QMimeData *mimeData)
    {
        insertFromMimeData(mimeData);
    }
};

bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    TestMessageEditor editor;
    QMimeData imageMime;
    imageMime.setImageData(QImage(4, 4, QImage::Format_ARGB32));

    int capabilityChecks = 0;
    int insertRequests = 0;
    QObject::connect(&editor, &MessageEditor::canInsertDataRequest,
        [&capabilityChecks](const QMimeData *, bool &canInsert) {
            ++capabilityChecks;
            canInsert = false;
        });
    QObject::connect(&editor, &MessageEditor::insertDataRequest,
        [&insertRequests](const QMimeData *, QTextDocument *) {
            ++insertRequests;
        });

    bool passed = true;
    passed &= check(!editor.canPaste(&imageMime),
        "a handler that rejects image MIME data prevents the editor from accepting it");
    passed &= check(capabilityChecks == 1,
        "the editor asks the handler before accepting image MIME data");

    editor.insertMime(&imageMime);
    passed &= check(insertRequests == 1,
        "the editor delegates image insertion to the registered handler");
    passed &= check(editor.document()->isEmpty(),
        "a handler that supplies no content leaves the editor unchanged");

    return passed ? 0 : 1;
}
