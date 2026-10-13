#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QTextLayout>

#include "spellbackend.h"
#include "spellhighlighter.h"

namespace {

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dictDir;
    if (!dictDir.isValid() ||
        !writeFile(dictDir.filePath("en_US.aff"), "SET UTF-8\n") ||
        !writeFile(dictDir.filePath("en_US.dic"), "1\nhello\n"))
        return 1;

    SpellBackend *backend = SpellBackend::instance();
    backend->setCustomDictPath(dictDir.path());
    if (!backend->dictionaries().contains("en_US"))
        return 2;

    backend->setLang("en_US");
    if (!backend->available() || !backend->isCorrect("hello") || backend->isCorrect("helo"))
        return 3;

    QTextDocument document;
    document.setPlainText("hello helo");
    SpellHighlighter highlighter(&document, nullptr);
    highlighter.rehighlight();
    QCoreApplication::processEvents();

    const QList<QTextLayout::FormatRange> formats = document.firstBlock().layout()->formats();
    const bool highlightsOnlyMisspelledWord = formats.size() == 1 &&
        formats.first().start == 6 && formats.first().length == 4 &&
        formats.first().format.underlineStyle() == QTextCharFormat::SpellCheckUnderline;
    SpellBackend::destroyInstance();
    return highlightsOnlyMisspelledWord ? 0 : 4;
}
